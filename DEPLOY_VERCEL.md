# Vercel Deployment Guide (Frontend)

The React SPA is hosted on Vercel and talks directly to the production API
gateway over HTTPS.

## Prerequisites
- GitHub repository connected to Vercel
- Production API gateway running at `https://nimish-legal-rag.duckdns.org`

## Quick Deploy

1. **Connect the repository**
   - Go to https://vercel.com/new
   - Import the GitHub repository
   - Set the root directory to `frontend`

2. **Build settings** (Vercel auto-detects Vite; these are the defaults)
   - Framework Preset: **Vite**
   - Build Command: `npm run build`
   - Output Directory: `dist`
   - Install Command: `npm install`

3. **Environment variables** (Project Settings → Environment Variables)

   | Variable | Value | Notes |
   |----------|-------|-------|
   | `VITE_API_URL` | `https://nimish-legal-rag.duckdns.org` | Production, Preview, Development |
   | `VITE_API_KEY` | _(your API key)_ | **Only if** `API_KEY` is set on the gateway |

   `VITE_*` variables are baked into the bundle at build time, so redeploy
   after changing them.

4. **Deploy** — Vercel auto-detects Vite and builds the project.

## Post-Deploy Verification

1. Open the deployment URL.
2. Ask a question and confirm the answer renders with citations.
3. Check the browser Network tab: requests go to `https://nimish-legal-rag.duckdns.org`.

## CORS

The gateway must allow the Vercel origin. On the EC2 host, set in `.env`:

```bash
# Exact production origin
CORS_ORIGINS=["https://distributed-legal-rag.vercel.app"]

# Optional: also allow Vercel preview deployments
CORS_ORIGIN_REGEX=^https://.*\.vercel\.app$
```

The gateway reads `CORS_ORIGINS` from `.env` at container start, so you can
change the Vercel domain without rebuilding the image:

```bash
docker compose -f docker-compose.prod.yml up -d --no-deps api_gateway
```

## Troubleshooting

### CORS errors
- Confirm `CORS_ORIGINS` in `.env` matches the Vercel origin.
- Restart the gateway (`docker compose -f docker-compose.prod.yml up -d --no-deps api_gateway`).

### API connection errors
- Verify `VITE_API_URL` points at the correct backend.
- Check that the gateway `/health` endpoint is reachable.

## Git Integration

Vercel's built-in Git integration deploys on every push to `main` and creates
preview deployments for pull requests. No extra workflow is required. If you
prefer an explicit workflow, see `.github/workflows/deploy.yml` for the
backend build/deploy pattern.
