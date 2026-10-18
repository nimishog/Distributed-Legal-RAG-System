import '@testing-library/jest-dom';
import React from 'react';

// jsdom does not implement scrollIntoView (used by ChatInterface auto-scroll).
if (typeof window !== 'undefined' && window.HTMLElement && !window.HTMLElement.prototype.scrollIntoView) {
  window.HTMLElement.prototype.scrollIntoView = () => {};
}

const makeStyled = (tag) => {
  const Component = React.forwardRef((props, ref) => {
    // Strip styled-components transient props before hitting the DOM.
    const {
      children,
      $isUser,
      $isError,
      $active,
      $done,
      $index,
      ...rest
    } = props || {};
    return React.createElement(tag, { ...rest, ref }, children);
  });
  Component.displayName = `styled(${tag})`;
  // Support both `styled.div\`...\`` (tagged template, first arg is the strings
  // array) and direct component invocation `<Comp />`.
  return (...args) => {
    if (args.length && Array.isArray(args[0])) return Component;
    return Component(args[0]);
  };
};

vi.mock('styled-components', () => {
  const styledProxy = new Proxy(function () {}, {
    get(_, prop) {
      if (typeof prop === 'symbol') return undefined;
      if (prop === 'then') return undefined;
      if (prop === 'default') return styledProxy;
      return makeStyled(String(prop));
    },
  });
  return {
    __esModule: true,
    default: styledProxy,
    keyframes: (strings) => strings.join(''),
    css: (strings) => strings.join(''),
    createGlobalStyle: () => null,
    ThemeProvider: ({ children }) => children,
  };
});
