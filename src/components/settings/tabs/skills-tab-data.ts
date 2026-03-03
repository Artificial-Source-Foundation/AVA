/**
 * Skills Tab — Built-in Skill Definitions
 *
 * Default skills with instructions, file globs, and descriptions.
 * Separated from the UI component to respect the 300-line limit.
 */

import type { SkillActivationMode } from '../../../stores/settings/settings-types'

export interface BuiltInSkill {
  id: string
  name: string
  description: string
  fileGlobs: string[]
  instructions: string
  activation: SkillActivationMode
}

export const BUILT_IN_SKILLS: BuiltInSkill[] = [
  {
    id: 'react-patterns',
    name: 'React Patterns',
    description: 'Component composition, hooks best practices, and React 19 patterns.',
    fileGlobs: ['**/*.tsx', '**/*.jsx'],
    instructions:
      'Follow React 19 patterns: use functional components, hooks, proper key usage, and component composition. Prefer server components where applicable.',
    activation: 'auto',
  },
  {
    id: 'python-best-practices',
    name: 'Python Best Practices',
    description: 'PEP 8 style, type hints, async patterns, and Pythonic idioms.',
    fileGlobs: ['**/*.py'],
    instructions:
      'Follow PEP 8 style. Use type hints consistently. Prefer async/await for I/O. Use dataclasses, pathlib, and Pythonic idioms.',
    activation: 'auto',
  },
  {
    id: 'rust-safety',
    name: 'Rust Safety',
    description: 'Ownership rules, lifetime annotations, unsafe blocks, and error handling.',
    fileGlobs: ['**/*.rs'],
    instructions:
      'Respect ownership and borrowing. Minimize unsafe blocks. Use Result/Option for error handling. Prefer zero-cost abstractions.',
    activation: 'auto',
  },
  {
    id: 'go-conventions',
    name: 'Go Conventions',
    description: 'Go idioms, error handling, goroutine patterns, and module layout.',
    fileGlobs: ['**/*.go'],
    instructions:
      'Follow Go conventions: handle errors explicitly, use goroutines responsibly, keep interfaces small, use standard project layout.',
    activation: 'auto',
  },
  {
    id: 'typescript-strict',
    name: 'TypeScript Strict',
    description: 'Strict mode patterns, utility types, generics, and type narrowing.',
    fileGlobs: ['**/*.ts'],
    instructions:
      'Use strict TypeScript: no any, proper generics, discriminated unions, type narrowing, and utility types.',
    activation: 'auto',
  },
  {
    id: 'css-architecture',
    name: 'CSS Architecture',
    description: 'BEM methodology, CSS custom properties, responsive design, and specificity.',
    fileGlobs: ['**/*.css', '**/*.scss'],
    instructions:
      'Use BEM naming, CSS custom properties for theming, mobile-first responsive design, and minimal specificity.',
    activation: 'auto',
  },
  {
    id: 'docker-best-practices',
    name: 'Docker Best Practices',
    description: 'Multi-stage builds, layer caching, security scanning, and compose patterns.',
    fileGlobs: ['**/Dockerfile', '**/Dockerfile.*', '**/docker-compose*.yml'],
    instructions:
      'Use multi-stage builds, minimize layers, run as non-root, pin base image versions, and use .dockerignore.',
    activation: 'auto',
  },
  {
    id: 'sql-optimization',
    name: 'SQL Optimization',
    description: 'Query optimization, indexing strategies, joins, and schema design.',
    fileGlobs: ['**/*.sql'],
    instructions:
      'Optimize queries: use proper indexes, avoid SELECT *, prefer JOINs over subqueries, normalize schema, and use EXPLAIN.',
    activation: 'auto',
  },
]
