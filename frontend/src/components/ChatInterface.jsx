import { useState, useRef, useEffect, useCallback } from 'react';
import MessageBubble from './MessageBubble';
import LoadingIndicator from './LoadingIndicator';
import ErrorMessage from './ErrorMessage';
import styled from 'styled-components';

const ChatContainer = styled.div`
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
  overflow: hidden;
`;

const MessagesArea = styled.div`
  flex: 1;
  overflow-y: auto;
  padding: 1rem;
  display: flex;
  flex-direction: column;
  gap: 1rem;
  scroll-behavior: smooth;

  &::-webkit-scrollbar {
    width: 6px;
  }

  &::-webkit-scrollbar-track {
    background: transparent;
  }

  &::-webkit-scrollbar-thumb {
    background: #374151;
    border-radius: 3px;
  }
`;

const InputArea = styled.div`
  border-top: 1px solid #1f2937;
  padding-top: 1rem;
`;

const InputWrapper = styled.div`
  display: flex;
  gap: 0.75rem;
  align-items: flex-end;
`;

const TextArea = styled.textarea`
  flex: 1;
  min-height: 56px;
  max-height: 200px;
  padding: 0.875rem 1rem;
  background: #111827;
  border: 1px solid #374151;
  border-radius: 12px;
  color: white;
  font-family: 'Inter', system-ui, sans-serif;
  font-size: 0.9375rem;
  line-height: 1.5;
  resize: none;
  outline: none;
  transition: border-color 0.2s, box-shadow 0.2s;

  &::placeholder {
    color: #6b7280;
  }

  &:focus {
    border-color: #0ea5e9;
    box-shadow: 0 0 0 3px rgba(14, 165, 233, 0.15);
  }

  &:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }
`;

const SendButton = styled.button`
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #0ea5e9 0%, #0284c7 100%);
  border: none;
  border-radius: 12px;
  color: white;
  cursor: pointer;
  transition: transform 0.15s, box-shadow 0.2s;
  flex-shrink: 0;

  &:hover:not(:disabled) {
    transform: scale(1.02);
    box-shadow: 0 4px 12px rgba(14, 165, 233, 0.4);
  }

  &:active:not(:disabled) {
    transform: scale(0.98);
  }

  &:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  svg {
    width: 20px;
    height: 20px;
  }
`;

const ClearButton = styled.button`
  padding: 0.5rem 1rem;
  background: transparent;
  border: 1px solid #374151;
  border-radius: 8px;
  color: #9ca3af;
  font-size: 0.8125rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  flex-shrink: 0;

  &:hover {
    border-color: #ef4444;
    color: #ef4444;
  }
`;

const StatsBar = styled.div`
  display: flex;
  gap: 1.5rem;
  padding: 0.5rem 1rem;
  background: #111827;
  border: 1px solid #1f2937;
  border-radius: 8px;
  font-size: 0.75rem;
  color: #9ca3af;
  font-family: 'JetBrains Mono', monospace;
`;

const StatItem = styled.span`
  display: flex;
  align-items: center;
  gap: 0.375rem;

  strong {
    color: #e5e7eb;
    font-weight: 600;
  }
`;

function ChatInterface({ messages, isLoading, error, onSendMessage, onClearChat }) {
  const messagesEndRef = useRef(null);
  const textareaRef = useRef(null);
  const [inputValue, setInputValue] = useState('');

  const scrollToBottom = useCallback(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, []);

  useEffect(() => {
    scrollToBottom();
  }, [messages, isLoading, scrollToBottom]);

  const handleSubmit = (e) => {
    e.preventDefault();
    const trimmed = inputValue.trim();
    if (!trimmed || isLoading) return;
    onSendMessage(trimmed);
    setInputValue('');
  };

  const handleKeyDown = (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSubmit(e);
    }
  };

  const adjustHeight = () => {
    const textarea = textareaRef.current;
    if (textarea) {
      textarea.style.height = 'auto';
      textarea.style.height = Math.min(textarea.scrollHeight, 200) + 'px';
    }
  };

  const lastMessage = messages[messages.length - 1];
  const showStats = lastMessage && lastMessage.role === 'assistant' && !lastMessage.isError;

  return (
    <ChatContainer>
      <MessagesArea role="log" aria-live="polite" aria-label="Chat messages">
        {messages.length === 0 && (
          <div className="flex flex-col items-center justify-center h-full text-gray-500">
            <span className="text-6xl mb-4">⚖</span>
            <h2 className="text-lg font-medium text-white mb-2">Ask a legal question</h2>
            <p className="text-sm max-w-xs text-center">
              I can help with legal research using a distributed vector database
              and GPT-4o-mini. Try asking about statutes, case law, or legal concepts.
            </p>
            <div className="mt-6 flex flex-wrap gap-2 justify-center">
              {[
                "What is the statute of limitations for breach of contract in California?",
                "What are the elements of a negligence claim in New York?",
                "How does the discovery rule affect statute of limitations in federal court?",
              ].map((q, i) => (
                <button
                  key={i}
                  onClick={() => onSendMessage(q)}
                  disabled={isLoading}
                  className="px-3 py-1.5 text-xs bg-legal-accent border border-primary-700 rounded-lg text-gray-300 hover:text-white hover:border-primary-500 transition-all text-left max-w-xs"
                >
                  {q}
                </button>
              ))}
            </div>
          </div>
        )}
        {messages.map((msg, idx) => (
          <MessageBubble
            key={`${msg.role}-${idx}-${msg.content?.slice(0, 20)}`}
            message={msg}
            index={idx}
          />
        ))}
        {isLoading && <LoadingIndicator />}
        <div ref={messagesEndRef} />
      </MessagesArea>

      {showStats && lastMessage.searchLatencyMs && (
        <StatsBar>
          <StatItem>
            Search: <strong>{lastMessage.searchLatencyMs.toFixed(0)}ms</strong>
          </StatItem>
          <StatItem>
            LLM: <strong>{lastMessage.llmLatencyMs.toFixed(0)}ms</strong>
          </StatItem>
          <StatItem>
            Total: <strong>{lastMessage.totalLatencyMs.toFixed(0)}ms</strong>
          </StatItem>
          <StatItem>
            Model: <strong>{lastMessage.model}</strong>
          </StatItem>
        </StatsBar>
      )}

      <InputArea>
        <ErrorMessage message={error} onDismiss={() => {}} />
        <form onSubmit={handleSubmit}>
          <InputWrapper>
            <TextArea
              ref={textareaRef}
              value={inputValue}
              onChange={(e) => {
                setInputValue(e.target.value);
                adjustHeight();
              }}
              onKeyDown={handleKeyDown}
              placeholder="Ask a legal question... (Shift+Enter for new line)"
              disabled={isLoading}
              rows={1}
              aria-label="Legal question input"
            />
            <SendButton
              type="submit"
              disabled={isLoading || !inputValue.trim()}
              aria-label="Send question"
            >
              <svg
                xmlns="http://www.w3.org/2000/svg"
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                strokeWidth="2.5"
                strokeLinecap="round"
                strokeLinejoin="round"
              >
                <path d="M22 2L11 13" />
                <path d="M22 2l-7 20-4-9-9-4 20-7z" />
              </svg>
            </SendButton>
          </InputWrapper>
        </form>
        {messages.length > 0 && (
          <div className="mt-3 text-right">
            <ClearButton type="button" onClick={onClearChat}>
              Clear chat
            </ClearButton>
          </div>
        )}
      </InputArea>
    </ChatContainer>
  );
}

export default ChatInterface;