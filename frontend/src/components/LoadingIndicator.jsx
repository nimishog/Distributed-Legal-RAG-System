import styled, { keyframes } from 'styled-components';

const pulse = keyframes`
  0%, 80%, 100% { transform: scale(0); opacity: 0.5; }
  40% { transform: scale(1); opacity: 1; }
`;

const spin = keyframes`
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
`;

const Container = styled.div`
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1rem;
  padding: 2rem;
`;

const DotsContainer = styled.div`
  display: flex;
  gap: 0.375rem;
`;

const Dot = styled.div`
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #0ea5e9;
  animation: ${pulse} 1.4s ease-in-out infinite both;
  animation-delay: ${({ $index }) => $index * 0.16}s;
`;

const Spinner = styled.div`
  width: 32px;
  height: 32px;
  border: 3px solid #1f2937;
  border-top-color: #0ea5e9;
  border-radius: 50%;
  animation: ${spin} 0.8s linear infinite;
`;

const StatusText = styled.div`
  color: #9ca3af;
  font-size: 0.875rem;
  font-weight: 500;
`;

const ProgressSteps = styled.div`
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  width: 100%;
  max-width: 300px;
`;

const Step = styled.div`
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.5rem 0.75rem;
  background: ${({ $active }) => ($active ? 'rgba(14, 165, 233, 0.1)' : '#111827')};
  border: 1px solid ${({ $active }) => ($active ? '#0ea5e9' : '#1f2937')};
  border-radius: 8px;
  transition: all 0.3s;

  .step-icon {
    width: 20px;
    height: 20px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    background: ${({ $active, $done }) =>
      $done ? '#0ea5e9' : $active ? 'rgba(14, 165, 233, 0.2)' : '#1f2937'};
    border: 1px solid ${({ $active, $done }) =>
      $done ? '#0ea5e9' : $active ? '#0ea5e9' : '#374151'};
    color: ${({ $done }) => ($done ? 'white' : '#0ea5e9')};
    font-size: 0.625rem;
    flex-shrink: 0;
  }

  .step-text {
    color: ${({ $active }) => ($active ? '#e5e7eb' : '#9ca3af')};
    font-size: 0.8125rem;
    font-weight: ${({ $active }) => ($active ? 500 : 400)};
  }
`;

function LoadingIndicator({ message = 'Searching legal documents...' }) {
  const steps = [
    { key: 'embed', label: 'Embedding query...', icon: '🧠' },
    { key: 'search', label: 'Searching vector database...', icon: '🔍' },
    { key: 'rerank', label: 'Ranking results...', icon: '📊' },
    { key: 'generate', label: 'Generating answer with GPT-4o-mini...', icon: '✨' },
  ];

  return (
    <Container role="status" aria-live="polite" aria-label="Loading">
      <DotsContainer>
        <Dot $index={0} />
        <Dot $index={1} />
        <Dot $index={2} />
      </DotsContainer>
      <StatusText>{message}</StatusText>
      <ProgressSteps>
        {steps.map((step, idx) => (
          <Step key={step.key} $active={idx <= 1} $done={idx < 1}>
            <div className="step-icon">
              {idx < 1 ? (
                <Spinner style={{ width: 12, height: 12, borderWidth: 2 }} />
              ) : (
                <span>{step.icon}</span>
              )}
            </div>
            <span className="step-text">{step.label}</span>
          </Step>
        ))}
      </ProgressSteps>
    </Container>
  );
}

export default LoadingIndicator;