import { useState } from 'react';
import styled, { keyframes } from 'styled-components';

const fadeIn = keyframes`
  from { opacity: 0; transform: translateY(8px); }
  to { opacity: 1; transform: translateY(0); }
`;

const MessageWrapper = styled.div`
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  animation: ${fadeIn} 0.3s ease-out;
  max-width: 85%;
  align-self: ${({ $isUser }) => ($isUser ? 'flex-end' : 'flex-start')};
`;

const Bubble = styled.div`
  padding: 0.875rem 1.125rem;
  border-radius: 16px;
  font-size: 0.9375rem;
  line-height: 1.6;
  background: ${({ $isUser, $isError }) =>
    $isError
      ? '#7f1d1d'
      : $isUser
      ? 'linear-gradient(135deg, #0ea5e9 0%, #0284c7 100%)'
      : '#1f2937'};
  color: ${({ $isUser, $isError }) =>
    $isError ? '#fecaca' : $isUser ? 'white' : '#e5e7eb'};
  border: ${({ $isError }) => ($isError ? '1px solid #ef4444' : 'none')};
  box-shadow: ${({ $isUser }) =>
    $isUser ? '0 4px 16px rgba(14, 165, 233, 0.3)' : '0 2px 8px rgba(0, 0, 0, 0.2)'};
  border-bottom-${({ $isUser }) => ($isUser ? 'right' : 'left')}-radius: 4px;
  white-space: pre-wrap;
  word-wrap: break-word;
`;

const CitationButton = styled.button`
  display: inline-flex;
  align-items: center;
  gap: 0.25rem;
  padding: 0.125rem 0.5rem;
  background: rgba(255, 255, 255, 0.1);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 999px;
  color: ${({ $isUser }) => ($isUser ? 'rgba(255,255,255,0.9)' : '#93c5fd')};
  font-size: 0.75rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s;
  margin: 0 0.125rem;
  font-family: 'JetBrains Mono', monospace;

  &:hover {
    background: rgba(255, 255, 255, 0.2);
    border-color: rgba(255, 255, 255, 0.4);
    color: white;
  }
`;

const CitationsPanel = styled.div`
  margin-top: 0.75rem;
  padding: 0.75rem;
  background: #111827;
  border: 1px solid #1f2937;
  border-radius: 10px;
  font-size: 0.8125rem;
`;

const CitationItem = styled.div`
  padding: 0.5rem 0;
  border-bottom: 1px solid #1f2937;

  &:last-child {
    border-bottom: none;
    padding-bottom: 0;
  }
`;

const CitationHeader = styled.div`
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-bottom: 0.375rem;
  font-family: 'JetBrains Mono', monospace;
  font-size: 0.75rem;
  color: #9ca3af;
`;

const CitationIndex = styled.span`
  display: flex;
  align-items: center;
  justify-content: center;
  width: 20px;
  height: 20px;
  background: #0ea5e9;
  color: white;
  border-radius: 4px;
  font-weight: 600;
`;

const CitationMeta = styled.div`
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
`;

const CitationMetaItem = styled.span`
  background: #1f2937;
  padding: 0.125rem 0.5rem;
  border-radius: 4px;
  color: #9ca3af;
  font-size: 0.6875rem;
`;

const CitationText = styled.div`
  margin-top: 0.5rem;
  padding: 0.5rem;
  background: #0f172a;
  border-radius: 6px;
  color: #cbd5e1;
  line-height: 1.5;
  font-size: 0.8125rem;
  max-height: 150px;
  overflow-y: auto;
`;

const ExpandButton = styled.button`
  display: flex;
  align-items: center;
  justify-content: center;
  width: 100%;
  padding: 0.5rem;
  margin-top: 0.5rem;
  background: transparent;
  border: 1px solid #374151;
  border-radius: 8px;
  color: #9ca3af;
  font-size: 0.8125rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;

  &:hover {
    border-color: #0ea5e9;
    color: #0ea5e9;
  }
`;

function formatMetadata(metadata) {
  if (!metadata || typeof metadata !== 'object') return [];
  return Object.entries(metadata)
    .filter(([, v]) => v != null && v !== '')
    .map(([k, v]) => `${k}: ${v}`);
}

function MessageBubble({ message, index }) {
  const [expandedCitation, setExpandedCitation] = useState(null);
  const isUser = message.role === 'user';
  const isError = message.isError;
  const citations = message.citations || [];

  if (isUser) {
    return (
      <MessageWrapper $isUser>
        <Bubble $isUser $isError={false}>{message.content}</Bubble>
      </MessageWrapper>
    );
  }

  if (isError) {
    return (
      <MessageWrapper $isUser={false}>
        <Bubble $isUser={false} $isError>
          {message.content}
        </Bubble>
      </MessageWrapper>
    );
  }

  return (
    <MessageWrapper $isUser={false}>
      <Bubble $isUser={false} $isError={false}>
        {message.content}
      </Bubble>

      {citations.length > 0 && (
        <CitationsPanel>
          <div className="flex items-center justify-between mb-3">
            <span className="text-xs font-medium text-gray-400 uppercase tracking-wider">
              Sources ({citations.length})
            </span>
            <ExpandButton
              onClick={() => setExpandedCitation(expandedCitation === null ? 0 : null)}
            >
              {expandedCitation !== null ? 'Collapse sources' : 'Expand all sources'}
            </ExpandButton>
          </div>

          {citations.map((citation, idx) => (
            <CitationItem key={citation.source_id}>
              <CitationHeader>
                <CitationIndex>{citation.index}</CitationIndex>
                <span className="font-medium text-white">
                  {citation.metadata?.source || 'Unknown source'}
                </span>
                {citation.metadata?.section && (
                  <span className="text-gray-500">§ {citation.metadata.section}</span>
                )}
                {citation.metadata?.page && (
                  <span className="text-gray-500">p. {citation.metadata.page}</span>
                )}
                <span className="text-primary-400 ml-auto">
                  {citation.score ? (citation.score * 100).toFixed(1) + '%' : ''}
                </span>
              </CitationHeader>

              <CitationMeta>
                {formatMetadata(citation.metadata).map((item, i) => (
                  <CitationMetaItem key={i}>{item}</CitationMetaItem>
                ))}
              </CitationMeta>

              {(expandedCitation === idx || expandedCitation === 0) && citation.text && (
                <CitationText>{citation.text}</CitationText>
              )}

              {expandedCitation !== 0 && expandedCitation !== idx && citation.text && (
                <button
                  onClick={() => setExpandedCitation(idx)}
                  className="text-xs text-primary-400 hover:underline mt-2 inline-block"
                >
                  Show excerpt
                </button>
              )}
            </CitationItem>
          ))}
        </CitationsPanel>
      )}
    </MessageWrapper>
  );
}

export default MessageBubble;