/**
 * Delta9 Refactor Template
 *
 * Pre-built mission structure for code refactoring.
 * Covers analysis, incremental changes, and verification.
 */

import type { MissionTemplate } from './types.js'

// =============================================================================
// Refactor Template
// =============================================================================

export const refactorTemplate: MissionTemplate = {
  name: 'Code Refactor',
  description: 'Template for refactoring code with safety and verification',
  type: 'refactor',
  defaultComplexity: 'medium',
  suggestedCouncilMode: 'standard',
  tags: ['refactor', 'cleanup', 'improvement', 'technical-debt'],

  variables: [
    {
      name: '{{REFACTOR_TARGET}}',
      description: 'What is being refactored (module, component, pattern)',
      required: true,
      example: 'Authentication module',
    },
    {
      name: '{{REFACTOR_GOAL}}',
      description: 'Goal of the refactoring effort',
      required: true,
      example: 'Improve maintainability and reduce code duplication',
    },
    {
      name: '{{TARGET_FILES}}',
      description: 'Files or directories to refactor',
      required: false,
      example: 'src/auth/',
    },
    {
      name: '{{CONSTRAINTS}}',
      description: 'Constraints or requirements to maintain',
      required: false,
      example: 'Must maintain backward compatibility with existing API',
    },
  ],

  objectives: [
    // Objective 1: Analysis
    {
      description: 'Analyze {{REFACTOR_TARGET}} and plan refactoring approach',
      tasks: [
        {
          description: 'Analyze current code structure and identify issues',
          acceptanceCriteria: [
            'Current architecture documented',
            'Code smells identified',
            'Dependencies mapped',
          ],
          routeTo: 'RECON',
        },
        {
          description: 'Design target architecture',
          acceptanceCriteria: [
            'Target structure defined',
            'Migration path identified',
            'Breaking changes documented',
          ],
          routeTo: 'TACCOM',
          dependsOn: [0],
        },
        {
          description: 'Create refactoring plan with incremental steps',
          acceptanceCriteria: [
            'Steps ordered by dependency',
            'Each step is atomic and testable',
            'Rollback points identified',
          ],
          routeTo: 'TACCOM',
          dependsOn: [1],
        },
      ],
    },

    // Objective 2: Preparation
    {
      description: 'Prepare for safe refactoring of {{REFACTOR_TARGET}}',
      tasks: [
        {
          description: 'Ensure comprehensive test coverage exists',
          acceptanceCriteria: [
            'Current behavior is well-tested',
            'Test coverage is sufficient for safe refactoring',
            'Integration tests in place',
          ],
          routeTo: 'SENTINEL',
        },
        {
          description: 'Create baseline metrics',
          acceptanceCriteria: [
            'Performance baseline captured',
            'Code complexity metrics recorded',
            'Bundle size baseline (if applicable)',
          ],
        },
      ],
    },

    // Objective 3: Incremental Refactoring
    {
      description: 'Execute refactoring of {{REFACTOR_TARGET}} incrementally',
      tasks: [
        {
          description: 'Refactor internal structure without changing interfaces',
          acceptanceCriteria: [
            'Internal code improved',
            'Public interfaces unchanged',
            'All tests still pass',
          ],
        },
        {
          description: 'Update interfaces if needed (with deprecation warnings)',
          acceptanceCriteria: [
            'New interfaces implemented',
            'Old interfaces deprecated (not removed)',
            'Migration guide provided if breaking',
          ],
          dependsOn: [0],
        },
        {
          description: 'Clean up deprecated code and finalize',
          acceptanceCriteria: [
            'Deprecated code removed (if safe)',
            'Code is clean and follows patterns',
            'No dead code remaining',
          ],
          dependsOn: [1],
        },
      ],
    },

    // Objective 4: Verification
    {
      description: 'Verify refactoring maintains functionality and improves quality',
      tasks: [
        {
          description: 'Run full test suite and verify behavior',
          acceptanceCriteria: ['All tests pass', 'No regressions detected', 'Edge cases verified'],
          routeTo: 'SENTINEL',
        },
        {
          description: 'Compare metrics with baseline',
          acceptanceCriteria: [
            'Performance maintained or improved',
            'Code complexity reduced',
            'Bundle size maintained or reduced',
          ],
        },
        {
          description: 'Code review for quality',
          acceptanceCriteria: [
            'Code follows patterns',
            'No new code smells introduced',
            'Maintainability improved',
          ],
        },
      ],
    },

    // Objective 5: Documentation
    {
      description: 'Update documentation for {{REFACTOR_TARGET}}',
      tasks: [
        {
          description: 'Update code documentation and comments',
          acceptanceCriteria: [
            'JSDoc/TSDoc updated',
            'Complex logic explained',
            'Examples updated',
          ],
          routeTo: 'SCRIBE',
        },
        {
          description: 'Update architecture documentation',
          acceptanceCriteria: [
            'Architecture docs reflect new structure',
            'Migration notes provided if applicable',
            'Diagrams updated',
          ],
          routeTo: 'SCRIBE',
        },
      ],
    },
  ],
}

// =============================================================================
// Refactor Template Variants
// =============================================================================

/**
 * Quick refactor template for small improvements
 */
export const quickRefactorTemplate: MissionTemplate = {
  ...refactorTemplate,
  name: 'Quick Refactor',
  description: 'Template for small, focused refactoring tasks',
  defaultComplexity: 'low',
  suggestedCouncilMode: 'none',
  tags: ['refactor', 'quick', 'cleanup'],

  objectives: [
    {
      description: 'Refactor {{REFACTOR_TARGET}}',
      tasks: [
        {
          description: 'Analyze and refactor code',
          acceptanceCriteria: ['Code improved', 'Tests still pass', 'No breaking changes'],
        },
        {
          description: 'Verify changes',
          acceptanceCriteria: ['All tests pass', 'Build succeeds'],
          dependsOn: [0],
        },
      ],
    },
  ],
}

/**
 * Large-scale refactor template
 */
export const largeRefactorTemplate: MissionTemplate = {
  ...refactorTemplate,
  name: 'Large-Scale Refactor',
  description: 'Template for major architectural refactoring',
  defaultComplexity: 'critical',
  suggestedCouncilMode: 'xhigh',
  tags: ['refactor', 'architecture', 'major', 'migration'],

  variables: [
    ...refactorTemplate.variables,
    {
      name: '{{ROLLBACK_PLAN}}',
      description: 'Plan for rolling back if needed',
      required: true,
      example: 'Revert to previous commit, feature flag to disable',
    },
  ],
}

/**
 * Performance refactor template
 */
export const performanceRefactorTemplate: MissionTemplate = {
  ...refactorTemplate,
  name: 'Performance Refactor',
  description: 'Template for refactoring focused on performance improvement',
  tags: ['refactor', 'performance', 'optimization'],

  variables: [
    ...refactorTemplate.variables,
    {
      name: '{{PERFORMANCE_TARGET}}',
      description: 'Target performance improvement',
      required: true,
      example: 'Reduce page load time from 3s to under 1s',
    },
    {
      name: '{{METRICS}}',
      description: 'Metrics to track improvement',
      required: true,
      example: 'LCP, FID, CLS, TTFB',
    },
  ],

  objectives: [
    {
      description: 'Profile and identify performance bottlenecks in {{REFACTOR_TARGET}}',
      tasks: [
        {
          description: 'Profile current performance',
          acceptanceCriteria: [
            'Baseline metrics captured',
            'Bottlenecks identified',
            'Root causes documented',
          ],
        },
        {
          description: 'Design optimization strategy',
          acceptanceCriteria: [
            'Optimization approach defined',
            'Expected impact estimated',
            'Trade-offs documented',
          ],
          routeTo: 'TACCOM',
          dependsOn: [0],
        },
      ],
    },
    ...refactorTemplate.objectives.slice(2), // Reuse execution and verification
  ],
}

/**
 * Type safety refactor template
 */
export const typeSafetyRefactorTemplate: MissionTemplate = {
  ...refactorTemplate,
  name: 'Type Safety Refactor',
  description: 'Template for improving TypeScript type safety',
  tags: ['refactor', 'typescript', 'types', 'safety'],

  variables: [
    ...refactorTemplate.variables,
    {
      name: '{{STRICT_MODE}}',
      description: 'Whether to enable strict mode',
      required: false,
      example: 'yes',
    },
  ],

  objectives: [
    {
      description: 'Improve type safety in {{REFACTOR_TARGET}}',
      tasks: [
        {
          description: 'Audit current type usage and identify issues',
          acceptanceCriteria: [
            'any types documented',
            'Type assertions identified',
            'Missing types listed',
          ],
          routeTo: 'RECON',
        },
        {
          description: 'Add proper types and remove any usage',
          acceptanceCriteria: [
            'No any types remain',
            'Proper generics used',
            'Type inference leveraged',
          ],
          dependsOn: [0],
        },
        {
          description: 'Enable stricter TypeScript settings',
          acceptanceCriteria: [
            'strictNullChecks enabled',
            'noImplicitAny enabled',
            'All type errors resolved',
          ],
          dependsOn: [1],
        },
      ],
    },
    ...refactorTemplate.objectives.slice(3), // Verification and documentation
  ],
}
