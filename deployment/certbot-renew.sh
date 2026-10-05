#!/bin/bash
# Certbot auto-renewal script for Legal RAG
# Run via cron: 0 3 * * * /path/to/certbot-renew.sh >> /var/log/certbot-renew.log 2>&1

set -euo pipefail

DOMAIN="nimish-legal-rag.duckdns.org"
EMAIL="admin@example.com"  # Update with your email
NGINX_CONTAINER="legal-rag-nginx"
CERTBOT_CONTAINER="legal-rag-certbot"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"
}

log "Starting certbot renewal check..."

# Check if certificates are due for renewal (within 30 days)
if docker exec $CERTBOT_CONTAINER certbot certificates 2>/dev/null | grep -q "VALID: 30"; then
    log "Certificates not due for renewal yet."
    exit 0
fi

log "Attempting certificate renewal..."
if docker exec $CERTBOT_CONTAINER certbot renew --quiet --nginx; then
    log "Certificates renewed successfully."
    # Reload nginx to pick up new certificates
    docker exec $NGINX_CONTAINER nginx -s reload
    log "Nginx reloaded."
else
    log "Certificate renewal failed."
    exit 1
fi

log "Renewal process completed."