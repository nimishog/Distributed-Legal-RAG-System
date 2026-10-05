import '@testing-library/jest-dom';
import { vi } from 'vitest';
import React from 'react';

const createStyledComponent = (tag) => {
  const Component = ({ children, ...props }) => {
    const { $isUser, $isError, $active, $done, $index, ...rest } = props;
    return React.createElement(tag, rest, children);
  };
  Component.displayName = `styled(${tag})`;
  return Component;
};

const styled = new Proxy({}, {
  get: (_, tag) => createStyledComponent(tag),
});

styled.keyframes = (strings) => `keyframes-${strings.join('')}`;

vi.mock('styled-components', () => ({
  default: styled,
  ...styled,
}));

global.React = React;