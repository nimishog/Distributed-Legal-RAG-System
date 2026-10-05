#!/bin/bash
set -euo pipefail

# ==========================================
# AWS Bootstrap for Distributed Legal RAG
# Creates: VPC, Subnets, SG, EC2, IAM, ECR, CloudMap
# Region: ap-south-1 (Mumbai)
# ==========================================

REGION="ap-south-1"
PROJECT="legal-rag"
VPC_CIDR="10.0.0.0/16"
PUBLIC_SUBNET_1_CIDR="10.0.1.0/24"
PUBLIC_SUBNET_2_CIDR="10.0.2.0/24"
PRIVATE_SUBNET_1_CIDR="10.0.11.0/24"
PRIVATE_SUBNET_2_CIDR="10.0.12.0/24"
KEY_NAME="${PROJECT}-key"
INSTANCE_TYPE="t3.micro"
AMI_ID="ami-0c55b159cbfafe1f0"  # Amazon Linux 2023 (update for region)

echo "=== AWS Bootstrap for $PROJECT in $REGION ==="

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

# --- Private Subnets ---
echo "Creating private subnets..."
PRIV_SUBNET_1=$(aws ec2 create-subnet --vpc-id $VPC_ID --cidr-block $PRIVATE_SUBNET_1_CIDR --availability-zone ${REGION}a --region $REGION --query 'Subnet.SubnetId' --output text)
PRIV_SUBNET_2=$(aws ec2 create-subnet --vpc-id $VPC_ID --cidr-block $PRIVATE_SUBNET_2_CIDR --availability-zone ${REGION}b --region $REGION --query 'Subnet.SubnetId' --output text)
aws ec2 create-tags --resources $PRIV_SUBNET_1 --tags Key=Name,Value="${PROJECT}-private-1" --region $REGION
aws ec2 create-tags --resources $PRIV_SUBNET_2 --tags Key=Name,Value="${PROJECT}-private-2" --region $REGION

# --- Route Tables ---
echo "Creating route tables..."
PUB_RT=$(aws ec2 create-route-table --vpc-id $VPC_ID --region $REGION --query 'RouteTable.RouteTableId' --output text)
aws ec2 create-route --route-table-id $PUB_RT --destination-cidr-block 0.0.0.0/0 --gateway-id $IGW_ID --region $REGION
aws ec2 associate-route-table --route-table-id $PUB_RT --subnet-id $PUB_SUBNET_1 --region $REGION
aws ec2 associate-route-table --route-table-id $PUB_RT --subnet-id $PUB_SUBNET_2 --region $REGION
aws ec2 create-tags --resources $PUB_RT --tags Key=Name,Value="${PROJECT}-public-rt" --region $REGION

# NAT Gateway for private subnets (optional, for ECR pulls)
echo "Creating NAT Gateway..."
EIP_ALLOC=$(aws ec2 allocate-address --domain vpc --region $REGION --query 'AllocationId' --output text)
NAT_GW=$(aws ec2 create-nat-gateway --subnet-id $PUB_SUBNET_1 --allocation-id $EIP_ALLOC --region $REGION --query 'NatGateway.NatGatewayId' --output text)
aws ec2 wait nat-gateway-available --nat-gateway-ids $NAT_GW --region $REGION

PRIV_RT=$(aws ec2 create-route-table --vpc-id $VPC_ID --region $REGION --query 'RouteTable.RouteTableId' --output text)
aws ec2 create-route --route-table-id $PRIV_RT --destination-cidr-block 0.0.0.0/0 --nat-gateway-id $NAT_GW --region $REGION
aws ec2 associate-route-table --route-table-id $PRIV_RT --subnet-id $PRIV_SUBNET_1 --region $REGION
aws ec2 associate-route-table --route-table-id $PRIV_RT --subnet-id $PRIV_SUBNET_2 --region $REGION
aws ec2 create-tags --resources $PRIV_RT --tags Key=Name,Value="${PROJECT}-private-rt" --region $REGION

# --- Security Groups ---
echo "Creating security groups..."
SG_EC2=$(aws ec2 create-security-group --group-name "${PROJECT}-ec2-sg" --description "EC2 SG for nginx + API" --vpc-id $VPC_ID --region $REGION --query 'GroupId' --output text)
aws ec2 authorize-security-group-ingress --group-id $SG_EC2 --protocol tcp --port 22 --cidr 0.0.0.0/0 --region $REGION
aws ec2 authorize-security-group-ingress --group-id $SG_EC2 --protocol tcp --port 80 --cidr 0.0.0.0/0 --region $REGION
aws ec2 authorize-security-group-ingress --group-id $SG_EC2 --protocol tcp --port 443 --cidr 0.0.0.0/0 --region $REGION

SG_ECS=$(aws ec2 create-security-group --group-name "${PROJECT}-ecs-sg" --description "ECS tasks SG" --vpc-id $VPC_ID --region $REGION --query 'GroupId' --output text)
aws ec2 authorize-security-group-ingress --group-id $SG_ECS --protocol tcp --port 8080 --source-group $SG_EC2 --region $REGION
aws ec2 authorize-security-group-ingress --group-id $SG_ECS --protocol tcp --port 8081 --source-group $SG_ECS --region $REGION

SG_RDS=$(aws ec2 create-security-group --group-name "${PROJECT}-rds-sg" --description "RDS SG" --vpc-id $VPC_ID --region $REGION --query 'GroupId' --output text)
aws ec2 authorize-security-group-ingress --group-id $SG_RDS --protocol tcp --port 5432 --source-group $SG_ECS --region $REGION

# --- Key Pair ---
echo "Creating key pair..."
aws ec2 create-key-pair --key-name $KEY_NAME --region $REGION --query 'KeyMaterial' --output text > ${KEY_NAME}.pem
chmod 400 ${KEY_NAME}.pem
echo "Key saved to ${KEY_NAME}.pem"

# --- IAM Roles ---
echo "Creating IAM roles..."
cat > /tmp/ec2-trust.json <<EOF
{
  "Version": "2012-10-17",
  "Statement": [{"Effect": "Allow", "Principal": {"Service": "ec2.amazonaws.com"}, "Action": "sts:AssumeRole"}]
}
EOF
aws iam create-role --role-name "${PROJECT}-ec2-role" --assume-role-policy-document file:///tmp/ec2-trust.json --region $REGION 2>/dev/null || true
aws iam attach-role-policy --role-name "${PROJECT}-ec2-role" --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore --region $REGION
aws iam attach-role-policy --role-name "${PROJECT}-ec2-role" --policy-arn arn:aws:iam::aws:policy/CloudWatchAgentServerPolicy --region $REGION

cat > /tmp/ecs-task-trust.json <<EOF
{
  "Version": "2012-10-17",
  "Statement": [{"Effect": "Allow", "Principal": {"Service": "ecs-tasks.amazonaws.com"}, "Action": "sts:AssumeRole"}]
}
EOF
aws iam create-role --role-name "${PROJECT}-ecs-task-role" --assume-role-policy-document file:///tmp/ecs-task-trust.json --region $REGION 2>/dev/null || true
aws iam put-role-policy --role-name "${PROJECT}-ecs-task-role" --policy-name "${PROJECT}-ecr-access" --policy-document '{
  "Version": "2012-10-17",
  "Statement": [{"Effect": "Allow", "Action": ["ecr:GetAuthorizationToken", "ecr:BatchGetImage", "ecr:GetDownloadUrlForLayer"], "Resource": "*"}]
}' --region $REGION

cat > /tmp/ecs-execution-trust.json <<EOF
{
  "Version": "2012-10-17",
  "Statement": [{"Effect": "Allow", "Principal": {"Service": "ecs-tasks.amazonaws.com"}, "Action": "sts:AssumeRole"}]
}
EOF
aws iam create-role --role-name "${PROJECT}-ecs-execution-role" --assume-role-policy-document file:///tmp/ecs-execution-trust.json --region $REGION 2>/dev/null || true
aws iam attach-role-policy --role-name "${PROJECT}-ecs-execution-role" --policy-arn arn:aws:iam::aws:policy/service-role/AmazonECSTaskExecutionRolePolicy --region $REGION

# --- ECR Repositories ---
echo "Creating ECR repositories..."
for repo in cpp_router cpp_database api_gateway; do
  aws ecr create-repository --repository-name $repo --region $REGION 2>/dev/null || true
  aws ecr set-repository-policy --repository-name $repo --policy-text '{
    "Version": "2012-10-17",
    "Statement": [{"Effect": "Allow", "Principal": {"AWS": "*"}, "Action": ["ecr:GetDownloadUrlForLayer", "ecr:BatchGetImage", "ecr:BatchCheckLayerAvailability"], "Condition": {"StringEquals": {"aws:PrincipalAccount": "'$(aws sts get-caller-identity --query Account --output text)'"}}}]
  }' --region $REGION 2>/dev/null || true
done

# --- CloudMap Namespace ---
echo "Creating CloudMap namespace..."
NAMESPACE_ID=$(aws servicediscovery create-private-dns-namespace --name "${PROJECT}.local" --vpc $VPC_ID --region $REGION --query 'OperationId' --output text)
aws servicediscovery wait operation-success --operation-id $NAMESPACE_ID --region $REGION
NAMESPACE_ID=$(aws servicediscovery get-operation --operation-id $NAMESPACE_ID --region $REGION --query 'Operation.Targets.NAMESPACE' --output text)

# --- EC2 Instance (for nginx + API Gateway) ---
echo "Launching EC2 instance..."
USER_DATA=$(cat <<'EOF'
#!/bin/bash
yum update -y
yum install -y docker git
systemctl start docker
systemctl enable docker
usermod -aG docker ec2-user

# Install docker-compose
curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
chmod +x /usr/local/bin/docker-compose

# Install AWS CLI v2
curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
unzip awscliv2.zip
./aws/install

# Create swap
dd if=/dev/zero of=/swapfile bs=1M count=2048
chmod 600 /swapfile
mkswap /swapfile
swapon /swapfile
echo "/swapfile swap swap defaults 0 0" >> /etc/fstab

# Certbot
yum install -y certbot python3-certbot-nginx
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

# --- Output summary ---
cat <<EOF

=== Bootstrap Complete ===
VPC ID: $VPC_ID
Public Subnets: $PUB_SUBNET_1, $PUB_SUBNET_2
Private Subnets: $PRIV_SUBNET_1, $PRIV_SUBNET_2
Security Groups:
  EC2: $SG_EC2
  ECS: $SG_ECS
  RDS: $SG_RDS
EC2 Instance: $INSTANCE_ID ($PUBLIC_IP)
Key Pair: ${KEY_NAME}.pem (save securely!)
CloudMap Namespace: $NAMESPACE_ID

=== Next Steps ===
1. SSH to EC2: ssh -i ${KEY_NAME}.pem ec2-user@$PUBLIC_IP
2. On EC2: Clone repo, configure .env, run docker-compose.prod.yml
3. Add GitHub secrets for deployment workflow
4. Configure DNS (DuckDNS) to point to $PUBLIC_IP
5. Run certbot: sudo certbot --nginx -d your-domain.duckdns.org

EOF