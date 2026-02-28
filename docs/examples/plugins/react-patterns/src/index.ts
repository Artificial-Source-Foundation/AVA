/**
 * React Patterns Plugin
 *
 * Demonstrates: skill registration via events
 * Triggers on .tsx/.jsx files and provides React patterns guidance
 * in the system prompt.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

const REACT_PATTERNS_CONTENT = `
## React Patterns Guide

### Component Composition
- Prefer function components with hooks over class components.
- Use composition over inheritance: pass children or render props.
- Keep components small and focused on a single responsibility.

### Hooks Best Practices
- Call hooks at the top level, never inside loops or conditions.
- Use custom hooks to extract reusable stateful logic.
- Prefer useReducer for complex state with multiple sub-values.
- Memoize expensive computations with useMemo and callbacks with useCallback.

### State Management
- Lift state up only as far as necessary.
- Use context for truly global state (theme, auth, locale).
- Avoid prop drilling: prefer composition or context.

### Performance
- Use React.memo for components that render often with the same props.
- Lazy-load routes and heavy components with React.lazy + Suspense.
- Avoid inline object/array creation in JSX props.

### TypeScript Integration
- Define prop interfaces explicitly, export them for consumers.
- Use discriminated unions for variant components.
- Prefer generic components for reusable data containers.
`.trim()

export function activate(api: ExtensionAPI): Disposable {
  // Register the skill via the skills:register event
  api.emit('skills:register', {
    name: 'React Patterns',
    description: 'Component composition, hooks, and React best practices.',
    globs: ['**/*.tsx', '**/*.jsx'],
    content: REACT_PATTERNS_CONTENT,
    source: 'plugin:react-patterns',
  })

  api.log.info('React Patterns skill registered')

  return {
    dispose() {
      // Skills registered via events are cleaned up by the skills extension
    },
  }
}
