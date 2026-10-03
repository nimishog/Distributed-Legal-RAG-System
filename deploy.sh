#!/bin/bash
# ==========================================
# EC2 Bootstrap Script for Legal RAG
# Run on fresh Ubuntu 22.04 t3.micro instance
# Usage: curl -fsSL https://raw.githubusercontent.com/.../deploy.sh | bash
# ==========================================

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# ==========================================
# Configuration
# ==========================================
REPO_URL="https://github.com/yourusername/distributed-legal-rag.git"
PROJECT_DIR="/opt/legal-rag"
DUCKDNS_DOMAIN="nimish-legal-rag.duckdns.org"
CERTBOT_EMAIL="your-email@example.com"

# ==========================================
# Pre-flight checks
# ==========================================
if [[ $EUID -eq 0 ]]; then
   log_error "Don't run as root! Run as ubuntu user with sudo privileges."
   exit 1
fi

log_info "Starting Legal RAG deployment bootstrap..."

# ==========================================
# System updates & essential packages
# ==========================================
log_info "Updating system packages..."
sudo apt-get update -y
sudo apt-get upgrade -y
sudo apt-get install -y \
    curl \
    wget \
    git \
    htop \
    iotop \
    ncdu \
    jq \
    unzip \
    ca-certificates \
    gnupg \
    lsb-release \
    software-properties-common

# ==========================================
# Create 2GB swap file (critical for t3.micro)
# ==========================================
log_info "Creating 2GB swap file..."
if [ ! -f /swapfile ]; then
    sudo fallocate -l 2G /swapfile
    sudo chmod 600 /swapfile
    sudo mkswap /swapfile
    sudo swapon /swapfile
    echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
    log_info "Swap created and enabled"
else
    log_warn "Swap file already exists"
fi

# Tune swappiness (prefer RAM, swap only when needed)
log_info "Tuning kernel parameters..."
echo 'vm.swappiness=10' | sudo tee -a /etc/sysctl.conf
echo 'vm.vfs_cache_pressure=50' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p

# ==========================================
# Install Docker
# ==========================================
log_info "Installing Docker..."
if ! command -v docker &> /dev/null; then
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
    sudo apt-get update -y
    sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
    sudo usermod -aG docker $USER
    log_info "Docker installed. You may need to log out and back in for group changes."
else
    log_warn "Docker already installed"
fi

# ==========================================
# Install Docker Compose (standalone)
# ==========================================
log_info "Installing Docker Compose..."
if ! command -v docker-compose &> /dev/null; then
    DOCKER_COMPOSE_VERSION="v2.24.0"
    sudo curl -L "https://github.com/docker/compose/releases/download/${DOCKER_COMPOSE_VERSION}/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
    sudo chmod +x /usr/local/bin/docker-compose
    log_info "Docker Compose installed"
else
    log_warn "Docker Compose already installed"
fi

# ==========================================
# Install k6 (for local load testing)
# ==========================================
log_info "Installing k6..."
if ! command -v k6 &> /dev/null; then
    sudo gpg -k
    sudo gpg --no-default-keyring --keyring /usr/share/keyrings/k6-archive-keyring.gpg --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys C5AD17C747E3415A3642D57D77C6C491D6AC1D69
    echo "deb [signed-by=/usr/share/keyrings/k6-archive-keyring.gpg] https://dl.k6.io/deb stable main" | sudo tee /etc/apt/sources.list.d/k6.list
    sudo apt-get update -y
    sudo apt-get install -y k6
    log_info "k6 installed"
else
    log_warn "k6 already installed"
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
    git pull origin main
fi

# ==========================================
# Create .env from template
# ==========================================
log_info "Setting up environment..."
if [ ! -f .env ]; then
    cp .env.example .env
    log_warn "Created .env from template. YOU MUST EDIT IT with real values:"
    log_warn "  - OPENAI_API_KEY"
    log_warn "  - DB_PASSWORD"
    log_warn "  - CERTBOT_EMAIL"
else
    log_info ".env already exists"
fi

# ==========================================
# Create certbot directories
# ==========================================
log_info "Creating certbot directories..."
mkdir -p certbot/conf certbot/www

# ==========================================
# Pull Docker images
# ==========================================
log_info "Pulling Docker images..."
docker compose -f docker-compose.prod.yml pull

# ==========================================
# Start services
# ==========================================
log_info "Starting services..."
docker compose -f docker-compose.prod.yml up -d

# ==========================================
# Wait for services to be healthy
# ==========================================
log_info "Waiting for services to become healthy..."
sleep 30

# Check health
for i in {1..10}; do
    if curl -sf http://localhost:8000/health > /dev/null 2>&1; then
        log_info "API Gateway is healthy!"
        break
    fi
    log_info "Waiting for API Gateway... (attempt $i/10)"
    sleep 10
done

# ==========================================
# Obtain SSL certificate
# ==========================================
log_info "Obtaining Let's Encrypt certificate..."
if [ ! -d "certbot/conf/live/$DUCKDNS_DOMAIN" ]; then
    log_warn "Ensure DuckDNS A record points to this instance's public IP!"
    read -p "Press Enter when DNS is ready, or Ctrl+C to skip..."

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
docker compose -f docker-compose.prod.yml restart nginx

# ==========================================
# Setup certbot auto-renewal cron
# ==========================================
log_info "Setting up certbot auto-renewal..."
(crontab -l 2>/dev/null; echo "0 3 * * * cd $PROJECT_DIR && docker compose -f docker-compose.prod.yml run --rm certbot renew --quiet && docker compose -f docker-compose.prod.yml restart nginx") | crontab -

# ==========================================
# Setup log rotation
# ==========================================
log_info "Setting up log rotation..."
sudo tee /etc/logrotate.d/legal-rag > /dev/null <<EOF
$PROJECT_DIR/logs/*.log {
    daily
    missingok
    rotate 14
    compress
    delaycompress
    notifempty
    create 644 ubuntu ubuntu
}
EOF

# ==========================================
# Final verification
# ==========================================
log_info "Running final health checks..."
sleep 10

echo ""
echo "=========================================="
echo "Deployment Status Check"
echo "=========================================="
docker compose -f docker-compose.prod.yml ps

echo ""
echo "Health Endpoints:"
curl -sf http://localhost:8000/health | jq . 2>/dev/null || echo "API Gateway: DOWN"
curl -sf http://localhost:8080/health | jq . 2>/dev/null || echo "Router: DOWN"

echo ""
echo "=========================================="
log_info "Bootstrap complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. Edit $PROJECT_DIR/.env with your secrets"
echo "2. Run ingestion: cd $PROJECT_DIR && docker compose -f docker-compose.ingestion.yml up --build --abort-on-container-exit"
echo "3. Test: curl https://$DUCKDNS_DOMAIN/health"
echo "4. Monitor: htop (check swap usage)"
echo ""
echo "To update deployment later:"
echo "  cd $PROJECT_DIR && git pull && docker compose -f docker-compose.prod.yml pull && docker compose -f docker-compose.prod.yml up -d"