#!/bin/bash
# ==========================================
# EC2 Bootstrap Script for Legal RAG
# Run on a fresh Amazon Linux 2023 t3.micro instance.
# Usage: REPO_URL=... DB_PASSWORD=... OPENAI_API_KEY=... ./deploy.sh
# ==========================================

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# ==========================================
# Configuration (override via environment)
# ==========================================
REPO_URL="${REPO_URL:-https://github.com/yourusername/distributed-legal-rag.git}"
PROJECT_DIR="${PROJECT_DIR:-/opt/legal-rag}"
DUCKDNS_DOMAIN="${DUCKDNS_DOMAIN:-nimish-legal-rag.duckdns.org}"
CERTBOT_EMAIL="${CERTBOT_EMAIL:-you@example.com}"
GITHUB_ACTOR="${GITHUB_ACTOR:-$USER}"

: "${DB_PASSWORD:?set DB_PASSWORD in the environment}"
: "${OPENAI_API_KEY:?set OPENAI_API_KEY in the environment}"

if [[ $EUID -eq 0 ]]; then
   log_error "Don't run as root! Run as ec2-user/ubuntu with sudo privileges."
   exit 1
fi

log_info "Starting Legal RAG deployment bootstrap..."

# ==========================================
# System updates & essential packages
# ==========================================
log_info "Updating system packages..."
sudo yum update -y || sudo apt-get update -y && sudo apt-get upgrade -y
if command -v yum >/dev/null 2>&1; then
    sudo yum install -y curl wget git jq unzip
else
    sudo apt-get install -y curl wget git jq unzip
fi

# ==========================================
# Create 2GB swap file (critical for t3.micro)
# ==========================================
log_info "Ensuring 2GB swap file..."
if [ ! -f /swapfile ]; then
    sudo fallocate -l 2G /swapfile || sudo dd if=/dev/zero of=/swapfile bs=1M count=2048
    sudo chmod 600 /swapfile
    sudo mkswap /swapfile
    sudo swapon /swapfile
    echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
    log_info "Swap created and enabled"
else
    log_warn "Swap file already exists"
fi
echo 'vm.swappiness=10' | sudo tee -a /etc/sysctl.conf
echo 'vm.vfs_cache_pressure=50' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p || true

# ==========================================
# Install Docker (if missing)
# ==========================================
log_info "Installing Docker..."
if ! command -v docker &> /dev/null; then
    if command -v yum >/dev/null 2>&1; then
        sudo yum install -y docker
        sudo systemctl start docker
        sudo systemctl enable docker
    else
        curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg
        echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
        sudo apt-get update -y
        sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
    fi
    sudo usermod -aG docker "$USER"
    log_info "Docker installed. You may need to log out and back in for group changes."
else
    log_warn "Docker already installed"
fi

# ==========================================
# Clone repository
# ==========================================
log_info "Cloning repository..."
if [ ! -d "$PROJECT_DIR" ]; then
    git clone "$REPO_URL" "$PROJECT_DIR"
    cd "$PROJECT_DIR"
else
    log_warn "Repository already exists, pulling latest..."
    cd "$PROJECT_DIR"
    git fetch origin main
    git reset --hard origin/main
fi

# ==========================================
# File-based secrets (no Swarm required)
# ==========================================
log_info "Writing secrets to secrets/ (mode 700, umask 077)..."
mkdir -p secrets
chmod 700 secrets
umask 077

write_secret() {
    local name="$1" val="$2"
    local file="secrets/$name"
    if [ ! -f "$file" ] || [ "$(cat "$file")" != "$val" ]; then
        printf '%s' "$val" > "$file"
    fi
}
write_secret DB_PASSWORD "$DB_PASSWORD"
write_secret openai_api_key "$OPENAI_API_KEY"
if [ -n "${API_KEY:-}" ]; then
    write_secret api_key "$API_KEY"
fi

# ==========================================
# Optional GHCR login (only needed for private packages)
# ==========================================
if [ -n "${GHCR_TOKEN:-}" ]; then
    log_info "Logging in to GHCR..."
    echo "$GHCR_TOKEN" | docker login ghcr.io -u "$GITHUB_ACTOR" --password-stdin
fi

# ==========================================
# Certbot directories
# ==========================================
mkdir -p certbot/conf certbot/www

# ==========================================
# Pull images and start services
# ==========================================
log_info "Pulling Docker images..."
IMAGE_TAG="${IMAGE_TAG:-latest}" docker compose -f docker-compose.prod.yml pull

log_info "Starting services..."
IMAGE_TAG="${IMAGE_TAG:-latest}" docker compose -f docker-compose.prod.yml up -d

# ==========================================
# Obtain SSL certificate
# ==========================================
log_info "Obtaining Let's Encrypt certificate..."
if [ ! -d "certbot/conf/live/$DUCKDNS_DOMAIN" ]; then
    log_warn "Ensure your DNS A record points to this instance's public IP!"
    sudo docker run --rm \
        -v "$(pwd)/certbot/conf:/etc/letsencrypt" \
        -v "$(pwd)/certbot/www:/var/www/certbot" \
        certbot/certbot certonly \
        --webroot -w /var/www/certbot \
        --email "$CERTBOT_EMAIL" \
        --agree-tos --no-eff-email \
        -d "$DUCKDNS_DOMAIN" || log_error "Certbot failed. Check DNS and try manually."
else
    log_info "Certificate already exists"
fi

# ==========================================
# Reload nginx with SSL
# ==========================================
log_info "Reloading Nginx with SSL..."
IMAGE_TAG="${IMAGE_TAG:-latest}" docker compose -f docker-compose.prod.yml restart nginx

# ==========================================
# Health gate through nginx -> api_gateway
# ==========================================
log_info "Waiting for the stack to become healthy (via nginx)..."
healthy=0
for _ in $(seq 1 30); do
    code=$(curl -s -o /dev/null -w '%{http_code}' "https://$DUCKDNS_DOMAIN/health" || true)
    if [ "$code" = "200" ]; then
        healthy=1
        log_info "Stack is healthy!"
        break
    fi
    log_info "Waiting... (last HTTP status: ${code:-none})"
    sleep 5
done
if [ "$healthy" != "1" ]; then
    log_error "Health check failed. Inspect logs with: docker compose -f docker-compose.prod.yml logs"
fi

# ==========================================
# Free disk space from dangling images
# ==========================================
log_info "Pruning dangling images..."
docker image prune -f || true

# ==========================================
# Certbot auto-renewal cron
# ==========================================
log_info "Setting up certbot auto-renewal..."
( crontab -l 2>/dev/null | grep -v 'certbot renew' ; \
  echo "0 3 * * * cd $PROJECT_DIR && docker compose -f docker-compose.prod.yml restart certbot && docker compose -f docker-compose.prod.yml restart nginx" ) | crontab -

# ==========================================
# Log rotation
# ==========================================
sudo tee /etc/logrotate.d/legal-rag > /dev/null <<EOF
$PROJECT_DIR/logs/*.log {
    daily
    missingok
    rotate 14
    compress
    delaycompress
    notifempty
    create 644 ec2-user ec2-user
}
EOF

# ==========================================
# Final verification
# ==========================================
echo ""
echo "=========================================="
echo "Deployment Status"
echo "=========================================="
IMAGE_TAG="${IMAGE_TAG:-latest}" docker compose -f docker-compose.prod.yml ps
echo ""
curl -sf "https://$DUCKDNS_DOMAIN/health" | jq . 2>/dev/null || echo "API Gateway: DOWN"
curl -sf "https://$DUCKDNS_DOMAIN/system-health" | jq . 2>/dev/null || echo "System health: DOWN"

echo ""
echo "=========================================="
log_info "Bootstrap complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. Run ingestion: cd $PROJECT_DIR && docker compose -f docker-compose.prod.yml --profile ingest run --rm ingestion"
echo "2. Test: curl https://$DUCKDNS_DOMAIN/health"
echo "3. Monitor: htop (check swap usage)"
echo ""
echo "To update later:"
echo "  cd $PROJECT_DIR && git pull && ./deploy.sh"
