import styled, { keyframes } from 'styled-components';

const slideDown = keyframes`
  from { opacity: 0; transform: translateY(-10px); }
  to { opacity: 1; transform: translateY(0); }
`;

const Container = styled.div`
  animation: ${slideDown} 0.2s ease-out;
`;

const Banner = styled.div`
  display: flex;
  align-items: flex-start;
  gap: 0.75rem;
  padding: 0.875rem 1rem;
  background: rgba(239, 68, 68, 0.1);
  border: 1px solid rgba(239, 68, 68, 0.3);
  border-radius: 10px;
  color: #fca5a5;
`;

const IconWrapper = styled.div`
  flex-shrink: 0;
  width: 20px;
  height: 20px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #ef4444;
`;

const Content = styled.div`
  flex: 1;
  min-width: 0;
`;

const Title = styled.div`
  font-weight: 600;
  font-size: 0.875rem;
  color: #fecaca;
  margin-bottom: 0.25rem;
`;

const Message = styled.div`
  font-size: 0.8125rem;
  line-height: 1.5;
  word-break: break-word;
`;

const DismissButton = styled.button`
  flex-shrink: 0;
  padding: 0.25rem;
  background: transparent;
  border: none;
  color: #fca5a5;
  cursor: pointer;
  opacity: 0.7;
  transition: opacity 0.2s;

  &:hover {
    opacity: 1;
  }

  svg {
    width: 16px;
    height: 16px;
  }
`;

function ErrorMessage({ message, onDismiss }) {
  if (!message) return null;

  return (
    <Container>
      <Banner role="alert">
        <IconWrapper>
          <svg
            xmlns="http://www.w3.org/2000/svg"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
          >
            <circle cx="12" cy="12" r="10" />
            <line x1="12" y1="8" x2="12" y2="12" />
            <line x1="12" y1="16" x2="12.01" y2="16" />
          </svg>
        </IconWrapper>
        <Content>
          <Title>Request failed</Title>
          <Message>{message}</Message>
        </Content>
        <DismissButton onClick={onDismiss} aria-label="Dismiss error">
          <svg
            xmlns="http://www.w3.org/2000/svg"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
          >
            <line x1="18" y1="6" x2="6" y2="18" />
            <line x1="6" y1="6" x2="18" y2="18" />
          </svg>
        </DismissButton>
      </Banner>
    </Container>
  );
}

export default ErrorMessage;