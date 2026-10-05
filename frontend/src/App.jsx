import { useState, useCallback } from 'react';
import ChatInterface from './components/ChatInterface';
import { apiClient } from './api/axios_client';

function App() {
  const [messages, setMessages] = useState([]);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState(null);

  const sendMessage = useCallback(async (question) => {
    const userMessage = { role: 'user', content: question };
    setMessages((prev) => [...prev, userMessage]);
    setIsLoading(true);
    setError(null);

    try {
      const response = await apiClient.post('/api/ask', {
        question,
        top_k: 5,
        include_citations: true,
      });

      const assistantMessage = {
        role: 'assistant',
        content: response.data.answer,
        citations: response.data.citations,
        searchLatencyMs: response.data.search_latency_ms,
        llmLatencyMs: response.data.llm_latency_ms,
        totalLatencyMs: response.data.total_latency_ms,
        model: response.data.model,
      };

      setMessages((prev) => [...prev, assistantMessage]);
    } catch (err) {
      const errorMessage = err.response?.data?.detail || err.message || 'Failed to get response';
      setError(errorMessage);
      setMessages((prev) => [
        ...prev,
        { role: 'assistant', content: `Error: ${errorMessage}`, isError: true },
      ]);
    } finally {
      setIsLoading(false);
    }
  }, []);

  const clearChat = useCallback(() => {
    setMessages([]);
    setError(null);
  }, []);

  return (
    <div className="min-h-screen bg-legal-darker flex flex-col">
      <header className="bg-legal-dark border-b border-legal-accent px-6 py-4">
        <div className="max-w-4xl mx-auto flex items-center justify-between">
          <div className="flex items-center gap-3">
            <span className="text-2xl">⚖</span>
            <h1 className="text-xl font-bold text-white">Legal RAG</h1>
            <span className="px-2 py-0.5 text-xs font-medium bg-primary-600 text-white rounded-full">
              Distributed
            </span>
          </div>
          <div className="flex items-center gap-4 text-sm text-gray-400">
            <span className="flex items-center gap-1">
              <span className="w-2 h-2 bg-green-500 rounded-full animate-pulse"></span>
              Connected
            </span>
          </div>
        </div>
      </header>

      <main className="flex-1 flex flex-col max-w-4xl mx-auto w-full p-6">
        <ChatInterface
          messages={messages}
          isLoading={isLoading}
          error={error}
          onSendMessage={sendMessage}
          onClearChat={clearChat}
        />
      </main>

      <footer className="bg-legal-dark border-t border-legal-accent px-6 py-3">
        <div className="max-w-4xl mx-auto text-center text-xs text-gray-500">
          Powered by Distributed Legal RAG • GPT-4o-mini • pgvector • C++17
        </div>
      </footer>
    </div>
  );
}

export default App;