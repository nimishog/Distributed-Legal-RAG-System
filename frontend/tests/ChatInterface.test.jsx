import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { apiClient, checkHealth, checkSystemHealth } from '../src/api/axios_client';
import ChatInterface from '../src/components/ChatInterface';

describe('API Client', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('creates axios instance with correct base URL', () => {
    expect(apiClient.defaults.baseURL).toBe('http://localhost:8000');
    expect(apiClient.defaults.timeout).toBe(60000);
    expect(apiClient.defaults.headers['Content-Type']).toBe('application/json');
  });

  it('checkHealth calls /health endpoint', async () => {
    const mockResponse = { data: { healthy: true, router_connected: true } };
    apiClient.get = vi.fn().mockResolvedValue(mockResponse);

    const result = await checkHealth();

    expect(apiClient.get).toHaveBeenCalledWith('/health');
    expect(result).toEqual({ healthy: true, router_connected: true });
  });

  it('checkSystemHealth calls /system-health endpoint', async () => {
    const mockResponse = { data: { cpu_percent: 10, memory_percent: 50 } };
    apiClient.get = vi.fn().mockResolvedValue(mockResponse);

    const result = await checkSystemHealth();

    expect(apiClient.get).toHaveBeenCalledWith('/system-health');
    expect(result).toEqual({ cpu_percent: 10, memory_percent: 50 });
  });

  it('has request interceptor configured', () => {
    expect(typeof apiClient.interceptors.request.use).toBe('function');
  });

  it('has response interceptor configured', () => {
    expect(typeof apiClient.interceptors.response.use).toBe('function');
  });

  it('handles 429 rate limit error', async () => {
    const error = { response: { status: 429, data: { detail: 'Rate limited' } } };
    apiClient.get = vi.fn().mockRejectedValue(error);

    await expect(checkHealth()).rejects.toMatchObject({ response: { status: 429 } });
  });
});

describe('ChatInterface component', () => {
  it('renders the input and send button', () => {
    render(
      <ChatInterface
        messages={[]}
        isLoading={false}
        error={null}
        onSendMessage={() => {}}
        onClearChat={() => {}}
        onDismissError={() => {}}
      />
    );

    expect(screen.getByLabelText('Legal question input')).toBeInTheDocument();
    expect(screen.getByLabelText('Send question')).toBeInTheDocument();
  });

  it('shows suggested questions when empty', () => {
    render(
      <ChatInterface
        messages={[]}
        isLoading={false}
        error={null}
        onSendMessage={() => {}}
        onClearChat={() => {}}
        onDismissError={() => {}}
      />
    );

    expect(screen.getByText(/Ask a legal question/i)).toBeInTheDocument();
  });

  it('sends a message on submit', () => {
    const onSendMessage = vi.fn();
    render(
      <ChatInterface
        messages={[]}
        isLoading={false}
        error={null}
        onSendMessage={onSendMessage}
        onClearChat={() => {}}
        onDismissError={() => {}}
      />
    );

    fireEvent.change(screen.getByLabelText('Legal question input'), {
      target: { value: 'What is a contract?' },
    });
    fireEvent.click(screen.getByLabelText('Send question'));

    expect(onSendMessage).toHaveBeenCalledWith('What is a contract?');
  });

  it('dismisses an error when the dismiss button is clicked', () => {
    const onDismissError = vi.fn();
    render(
      <ChatInterface
        messages={[]}
        isLoading={false}
        error="Something went wrong"
        onSendMessage={() => {}}
        onClearChat={() => {}}
        onDismissError={onDismissError}
      />
    );

    fireEvent.click(screen.getByLabelText('Dismiss error'));
    expect(onDismissError).toHaveBeenCalled();
  });
});