#!/bin/bash
set -euo pipefail

# ==========================================
# AWS Bootstrap for Distributed Legal RAG
# Creates: VPC, public subnets, SG, EC2 (t3.micro), IAM role, key pair.
# The EC2 host runs the entire stack via docker-compose.prod.yml.
# Region: ap-south-1 (Mumbai)
# ==========================================

REGION="ap-south-1"
PROJECT="legal-rag"
VPC_CIDR="10.0.0.0/16"
PUBLIC_SUBNET_1_CIDR="10.0.1.0/24"
PUBLIC_SUBNET_2_CIDR="10.0.2.0/24"
KEY_NAME="${PROJECT}-key"
INSTANCE_TYPE="t3.micro"

echo "=== AWS Bootstrap for $PROJECT in $REGION ==="

# Resolve the latest Amazon Linux 2023 AMI from SSM (region-correct).
echo "Resolving Amazon Linux 2023 AMI..."
AMI_ID=$(aws ssm get-parameter \
  --name /aws/service/ami-amazon-linux-latest/al2023-ami-kernel-default-x86_64 \
  --region "$REGION" --query 'Parameter.Value' --output text)
echo "AMI: $AMI_ID"

# --- VPC ---
echo "Creating VPC..."
VPC_ID=$(aws ec2 create-vpc --cidr-block $VPC_CIDR --region $REGION --query 'Vpc.VpcId' --output text)
aws ec2 create-tags --resources $VPC_ID --tags Key=Name,Value="${PROJECT}-vpc" --region $REGION
aws ec2 modify-vpc-attribute --vpc-id $VPC_ID --enable-dns-hostnames --region $REGION
aws ec2 modify-vpc-attribute --vpc-id $VPC_ID --enable-dns-support --region $REGION
echo "VPC: $VPC_ID"

# --- Internet Gateway ---
echo "Creating Internet Gateway..."
IGW_ID=$(aws ec2 create-internet-gateway --region $REGION --query 'InternetGateway.InternetGatewayId' --output text)
aws ec2 attach-internet-gateway --internet-gateway-id $IGW_ID --vpc-id $VPC_ID --region $REGION
aws ec2 create-tags --resources $IGW_ID --tags Key=Name,Value="${PROJECT}-igw" --region $REGION
echo "IGW: $IGW_ID"

# --- Public Subnets ---
echo "Creating public subnets..."
PUB_SUBNET_1=$(aws ec2 create-subnet --vpc-id $VPC_ID --cidr-block $PUBLIC_SUBNET_1_CIDR --availability-zone ${REGION}a --region $REGION --query 'Subnet.SubnetId' --output text)
PUB_SUBNET_2=$(aws ec2 create-subnet --vpc-id $VPC_ID --cidr-block $PUBLIC_SUBNET_2_CIDR --availability-zone ${REGION}b --region $REGION --query 'Subnet.SubnetId' --output text)
aws ec2 create-tags --resources $PUB_SUBNET_1 --tags Key=Name,Value="${PROJECT}-public-1" --region $REGION
aws ec2 create-tags --resources $PUB_SUBNET_2 --tags Key=Name,Value="${PROJECT}-public-2" --region $REGION
aws ec2 modify-subnet-attribute --subnet-id $PUB_SUBNET_1 --map-public-ip-on-launch --region $REGION
aws ec2 modify-subnet-attribute --subnet-id $PUB_SUBNET_2 --map-public-ip-on-launch --region $REGION

# --- Route Table ---
echo "Creating route table..."
PUB_RT=$(aws ec2 create-route-table --vpc-id $VPC_ID --region $REGION --query 'RouteTable.RouteTableId' --output text)
aws ec2 create-route --route-table-id $PUB_RT --destination-cidr-block 0.0.0.0/0 --gateway-id $IGW_ID --region $REGION
aws ec2 associate-route-table --route-table-id $PUB_RT --subnet-id $PUB_SUBNET_1 --region $REGION
aws ec2 associate-route-table --route-table-id $PUB_RT --subnet-id $PUB_SUBNET_2 --region $REGION
aws ec2 create-tags --resources $PUB_RT --tags Key=Name,Value="${PROJECT}-public-rt" --region $REGION

# --- Security Group ---
echo "Creating security group..."
SG_EC2=$(aws ec2 create-security-group --group-name "${PROJECT}-sg" --description "Legal RAG SG (SSH, HTTP, HTTPS)" --vpc-id $VPC_ID --region $REGION --query 'GroupId' --output text)
aws ec2 authorize-security-group-ingress --group-id $SG_EC2 --protocol tcp --port 22 --cidr 0.0.0.0/0 --region $REGION
aws ec2 authorize-security-group-ingress --group-id $SG_EC2 --protocol tcp --port 80 --cidr 0.0.0.0/0 --region $REGION
aws ec2 authorize-security-group-ingress --group-id $SG_EC2 --protocol tcp --port 443 --cidr 0.0.0.0/0 --region $REGION
echo "Security group: $SG_EC2"

# --- Key Pair ---
echo "Creating key pair..."
aws ec2 create-key-pair --key-name $KEY_NAME --region $REGION --query 'KeyMaterial' --output text > "${KEY_NAME}.pem"
chmod 400 "${KEY_NAME}.pem"
echo "Key saved to ${KEY_NAME}.pem"

# --- IAM Role (SSM + CloudWatch) ---
echo "Creating IAM role..."
cat > /tmp/ec2-trust.json <<EOF
{
  "Version": "2012-10-17",
  "Statement": [{"Effect": "Allow", "Principal": {"Service": "ec2.amazonaws.com"}, "Action": "sts:AssumeRole"}]
}
EOF
aws iam create-role --role-name "${PROJECT}-ec2-role" --assume-role-policy-document file:///tmp/ec2-trust.json --region $REGION 2>/dev/null || true
aws iam attach-role-policy --role-name "${PROJECT}-ec2-role" --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore --region $REGION
aws iam attach-role-policy --role-name "${PROJECT}-ec2-role" --policy-arn arn:aws:iam::aws:policy/CloudWatchAgentServerPolicy --region $REGION
aws iam create-instance-profile --instance-profile-name "${PROJECT}-ec2-role" --region $REGION 2>/dev/null || true
aws iam add-role-to-instance-profile --instance-profile-name "${PROJECT}-ec2-role" --role-name "${PROJECT}-ec2-role" --region $REGION 2>/dev/null || true

# --- EC2 Instance ---
echo "Launching EC2 instance..."
USER_DATA=$(cat <<'EOF'
#!/bin/bash
yum update -y
yum install -y docker git
systemctl start docker
systemctl enable docker
usermod -aG docker ec2-user

# Docker Compose plugin
mkdir -p /usr/local/lib/docker/cli-plugins
curl -SL "https://github.com/docker/compose/releases/latest/download/docker-compose-linux-x86_64" \
  -o /usr/local/lib/docker/cli-plugins/docker-compose
chmod +x /usr/local/lib/docker/cli-plugins/docker-compose

# 2 GB swap (critical for the 1 GB t3.micro)
if [ ! -f /swapfile ]; then
  fallocate -l 2G /swapfile || dd if=/dev/zero of=/swapfile bs=1M count=2048
  chmod 600 /swapfile
  mkswap /swapfile
  swapon /swapfile
  echo "/swapfile swap swap defaults 0 0" >> /etc/fstab
fi

# Certbot
yum install -y certbot

# Clone the repository so a fresh deploy.sh run finds /opt/legal-rag populated.
mkdir -p /opt/legal-rag
git clone https://github.com/nimishog/Distributed-Legal-RAG-System.git /opt/legal-rag || true
chown -R ec2-user:ec2-user /opt/legal-rag
EOF
)

INSTANCE_ID=$(aws ec2 run-instances \
  --image-id $AMI_ID \
  --instance-type $INSTANCE_TYPE \
  --key-name $KEY_NAME \
  --security-group-ids $SG_EC2 \
  --subnet-id $PUB_SUBNET_1 \
  --iam-instance-profile Name="${PROJECT}-ec2-role" \
  --user-data "$USER_DATA" \
  --tag-specifications "ResourceType=instance,Tags=[{Key=Name,Value=${PROJECT}-ec2}]" \
  --region $REGION \
  --query 'Instances[0].InstanceId' --output text)

echo "Waiting for instance to be running..."
aws ec2 wait instance-running --instance-ids $INSTANCE_ID --region $REGION

PUBLIC_IP=$(aws ec2 describe-instances --instance-ids $INSTANCE_ID --region $REGION --query 'Reservations[0].Instances[0].PublicIpAddress' --output text)
echo "EC2 Public IP: $PUBLIC_IP"

cat <<EOF

=== Bootstrap Complete ===
VPC ID: $VPC_ID
Public Subnets: $PUB_SUBNET_1, $PUB_SUBNET_2
Security Group: $SG_EC2
EC2 Instance: $INSTANCE_ID ($PUBLIC_IP)
Key Pair: ${KEY_NAME}.pem (save securely!)

=== Next Steps ===
1. SSH to EC2: ssh -i ${KEY_NAME}.pem ec2-user@$PUBLIC_IP
2. On EC2: git clone the repo, create secrets/, run deploy.sh
3. Add GitHub secrets (EC2_HOST, EC2_SSH_KEY, DB_PASSWORD, OPENAI_API_KEY)
4. Point your DNS (DuckDNS) at $PUBLIC_IP
EOF
