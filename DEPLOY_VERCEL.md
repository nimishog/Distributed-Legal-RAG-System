# Vercel Deployment Guide for Legal RAG Frontend

## Prerequisites
- GitHub repository connected to Vercel
- Production API Gateway running at `https://nimish-legal-rag.duckdns.org` (or your domain)

## Quick Deploy

1. **Connect Repository to Vercel**
   - Go to https://vercel.com/new
   - Import your GitHub repository
   - Select the `frontend` directory as the root directory

2. **Configure Build Settings**
   - Framework Preset: **Vite**
   - Build Command: `npm run build`
   - Output Directory: `dist`
   - Install Command: `npm install`

3. **Set Environment Variables** (in Vercel Project Settings → Environment Variables)
   | Variable | Value | Environment |
   |----------|-------|-------------|
   | `VITE_API_URL` | `https://nimish-legal-rag.duckdns.org` | Production, Preview, Development |

4. **Deploy**
   - Click "Deploy"
   - Vercel will auto-detect Vite and build the project

## Post-Deploy Verification

1. Open the deployed URL (e.g., `https://your-project.vercel.app`)
2. Test the chat interface
3. Check browser console for API requests going to the correct backend

## Custom Domain (Optional)

1. In Vercel Project Settings → Domains
2. Add your custom domain (e.g., `legal-rag.yourdomain.com`)
3. Configure DNS as instructed by Vercel

## Environment-Specific Configuration

### Development
- Uses proxy in `vite.config.js` to `http://localhost:8000`
- No environment variables needed locally

### Preview (Vercel Preview Deployments)
- Automatically uses the `VITE_API_URL` from environment variables
- Set to staging API if available, otherwise production

### Production
- Uses production API URL
- Assets served with long-term caching via `vercel.json` headers

## Troubleshooting

### CORS Errors
If you see CORS errors, ensure the API Gateway has CORS configured:
```python
# In api_gateway/src/main.py
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Or specific Vercel domain
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)
```

### API Connection Issues
- Verify `VITE_API_URL` is set correctly in Vercel
- Check browser Network tab for failed requests
- Ensure API Gateway health endpoint is accessible

### Build Failures
- Check build logs in Vercel dashboard
- Ensure all dependencies are in `package.json`
- Verify Node.js version compatibility (Vercel uses 18.x by default)

## Vercel Configuration Reference

The `vercel.json` in the frontend directory handles:
- SPA routing (rewrites all routes to `index.html`)
- Asset caching (1 year for `/assets/*`)
- Framework detection (Vite)

```json
{
  "buildCommand": "npm run build",
  "outputDirectory": "dist",
  "framework": "vite",
  "rewrites": [
    {
      "source": "/(.*)",
      "destination": "/index.html"
    }
  ],
  "headers": [
    {
      "source": "/assets/(.*)",
      "headers": [
        {
          "key": "Cache-Control",
          "value": "public, max-age=31536000, immutable"
        }
      ]
    }
  ]
}
```

## CI/CD Integration

The GitHub Actions workflow can trigger Vercel deployments:
```yaml
# .github/workflows/deploy-frontend.yml
name: Deploy Frontend to Vercel
on:
  push:
    branches: [main]
    paths:
      - 'frontend/**'
jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: amondnet/vercel-action@v25
        with:
          vercel-token: ${{ secrets.VERCEL_TOKEN }}
          vercel-org-id: ${{ secrets.VERCEL_ORG_ID }}
          vercel-project-id: ${{ secrets.VERCEL_PROJECT_ID }}
          vercel-args: '--prod'
```