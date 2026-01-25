/**
 * Delta9 Feature Template
 *
 * Pre-built mission structure for implementing new features.
 * Covers analysis, implementation, testing, and documentation.
 */

import type { MissionTemplate } from './types.js'

// =============================================================================
// Feature Template
// =============================================================================

export const featureTemplate: MissionTemplate = {
  name: 'New Feature',
  description: 'Template for implementing a new feature with full lifecycle coverage',
  type: 'feature',
  defaultComplexity: 'medium',
  suggestedCouncilMode: 'standard',
  tags: ['feature', 'implementation', 'new-functionality'],

  variables: [
    {
      name: '{{FEATURE_NAME}}',
      description: 'Name of the feature being implemented',
      required: true,
      example: 'User Authentication',
    },
    {
      name: '{{FEATURE_DESCRIPTION}}',
      description: 'Brief description of what the feature does',
      required: true,
      example: 'Allow users to sign in with email/password or OAuth providers',
    },
    {
      name: '{{TARGET_AREA}}',
      description: 'Area of the codebase this feature affects',
      required: false,
      example: 'src/auth/',
    },
  ],

  objectives: [
    // Objective 1: Analysis & Planning
    {
      description: 'Analyze requirements and plan implementation for {{FEATURE_NAME}}',
      tasks: [
        {
          description: 'Review existing codebase for integration points',
          acceptanceCriteria: [
            'Identified relevant files and modules',
            'Documented existing patterns to follow',
            'Listed potential conflicts or dependencies',
          ],
          routeTo: 'RECON',
        },
        {
          description: 'Research best practices for {{FEATURE_NAME}}',
          acceptanceCriteria: [
            'Identified industry standards',
            'Documented recommended approaches',
            'Listed potential pitfalls to avoid',
          ],
          routeTo: 'SIGINT',
          dependsOn: [0],
        },
        {
          description: 'Create detailed implementation plan',
          acceptanceCriteria: [
            'Clear step-by-step implementation approach',
            'Identified all files to create/modify',
            'Estimated complexity and risks',
          ],
          routeTo: 'TACCOM',
          dependsOn: [0, 1],
        },
      ],
    },

    // Objective 2: Core Implementation
    {
      description: 'Implement core functionality for {{FEATURE_NAME}}',
      tasks: [
        {
          description: 'Create type definitions and interfaces',
          acceptanceCriteria: [
            'TypeScript types are complete and accurate',
            'Interfaces follow existing patterns',
            'No any types used',
          ],
        },
        {
          description: 'Implement core logic',
          acceptanceCriteria: [
            'Core functionality works as specified',
            'Follows existing code patterns',
            'Proper error handling in place',
          ],
          dependsOn: [0],
        },
        {
          description: 'Add integration with existing systems',
          acceptanceCriteria: [
            'Feature integrates with existing codebase',
            'No breaking changes to existing functionality',
            'Proper imports and exports',
          ],
          dependsOn: [1],
        },
      ],
    },

    // Objective 3: UI/Frontend (if applicable)
    {
      description: 'Implement UI components for {{FEATURE_NAME}}',
      tasks: [
        {
          description: 'Create UI components',
          acceptanceCriteria: [
            'Components follow design system',
            'Responsive design implemented',
            'Accessibility requirements met (WCAG AA)',
          ],
          routeTo: 'FACADE',
        },
        {
          description: 'Implement user interactions and state management',
          acceptanceCriteria: [
            'User flows work correctly',
            'Loading and error states handled',
            'State management follows patterns',
          ],
          routeTo: 'FACADE',
          dependsOn: [0],
        },
      ],
    },

    // Objective 4: Testing
    {
      description: 'Create comprehensive tests for {{FEATURE_NAME}}',
      tasks: [
        {
          description: 'Write unit tests',
          acceptanceCriteria: [
            'Core logic has >80% coverage',
            'Edge cases are tested',
            'Tests follow existing patterns',
          ],
          routeTo: 'SENTINEL',
        },
        {
          description: 'Write integration tests',
          acceptanceCriteria: [
            'Integration points are tested',
            'API contracts verified',
            'Error scenarios tested',
          ],
          routeTo: 'SENTINEL',
          dependsOn: [0],
        },
      ],
    },

    // Objective 5: Documentation
    {
      description: 'Document {{FEATURE_NAME}} for users and developers',
      tasks: [
        {
          description: 'Write developer documentation',
          acceptanceCriteria: [
            'API documentation is complete',
            'Code examples provided',
            'Integration guide included',
          ],
          routeTo: 'SCRIBE',
        },
        {
          description: 'Update user documentation',
          acceptanceCriteria: [
            'User guide updated',
            'Screenshots/diagrams if applicable',
            'FAQ/troubleshooting added',
          ],
          routeTo: 'SCRIBE',
        },
      ],
    },
  ],
}

// =============================================================================
// Feature Template Variants
// =============================================================================

/**
 * Simple feature template for smaller features
 */
export const simpleFeatureTemplate: MissionTemplate = {
  ...featureTemplate,
  name: 'Simple Feature',
  description: 'Template for implementing a small, focused feature',
  defaultComplexity: 'low',
  suggestedCouncilMode: 'quick',
  tags: ['feature', 'simple', 'quick'],

  objectives: [
    // Single objective combining analysis and implementation
    {
      description: 'Implement {{FEATURE_NAME}}',
      tasks: [
        {
          description: 'Review codebase and implement feature',
          acceptanceCriteria: [
            'Feature works as specified',
            'Follows existing patterns',
            'No breaking changes',
          ],
        },
        {
          description: 'Add basic tests',
          acceptanceCriteria: [
            'Core functionality tested',
            'Tests pass',
          ],
          routeTo: 'SENTINEL',
          dependsOn: [0],
        },
      ],
    },
  ],
}

/**
 * Complex feature template for large features
 */
export const complexFeatureTemplate: MissionTemplate = {
  ...featureTemplate,
  name: 'Complex Feature',
  description: 'Template for implementing a large, multi-faceted feature',
  defaultComplexity: 'critical',
  suggestedCouncilMode: 'xhigh',
  tags: ['feature', 'complex', 'critical'],

  variables: [
    ...featureTemplate.variables,
    {
      name: '{{MIGRATION_NEEDED}}',
      description: 'Whether database/schema migrations are needed',
      required: false,
      example: 'yes',
    },
  ],
}
