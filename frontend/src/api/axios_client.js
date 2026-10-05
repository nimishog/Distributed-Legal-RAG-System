import axios from 'axios';

const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:8000';

export const apiClient = axios.create({
  baseURL: API_URL,
  timeout: 60000,
  headers: {
    'Content-Type': 'application/json',
    Accept: 'application/json',
  },
});

apiClient.interceptors.request.use(
  (config) => {
    const requestId = crypto.randomUUID?.() || Math.random().toString(36).substring(7);
    config.headers['X-Request-ID'] = requestId;
    return config;
  },
  (error) => Promise.reject(error)
);

apiClient.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response) {
      const status = error.response.status;
      const message = error.response.data?.detail || error.message;

      if (status === 429) {
        console.warn('Rate limited:', message);
      } else if (status === 503) {
        console.warn('Service unavailable:', message);
      } else if (status >= 500) {
        console.error('Server error:', message);
      }
    } else if (error.request) {
      console.error('Network error: No response received');
    } else {
      console.error('Request setup error:', error.message);
    }

    return Promise.reject(error);
  }
);

export async function checkHealth() {
  const response = await apiClient.get('/health');
  return response.data;
}

export async function checkSystemHealth() {
  const response = await apiClient.get('/system-health');
  return response.data;
}

export default apiClient;