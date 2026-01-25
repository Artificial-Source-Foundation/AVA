/**
 * Delta9 Bugfix Template
 *
 * Pre-built mission structure for fixing bugs.
 * Covers reproduction, root cause analysis, fix, and verification.
 */

import type { MissionTemplate } from './types.js'

// =============================================================================
// Bugfix Template
// =============================================================================

export const bugfixTemplate: MissionTemplate = {
  name: 'Bug Fix',
  description: 'Template for diagnosing and fixing bugs with proper verification',
  type: 'bugfix',
  defaultComplexity: 'medium',
  suggestedCouncilMode: 'quick',
  tags: ['bugfix', 'fix', 'issue', 'debug'],

  variables: [
    {
      name: '{{BUG_DESCRIPTION}}',
      description: 'Description of the bug and its symptoms',
      required: true,
      example: 'Login fails with "Invalid credentials" even with correct password',
    },
    {
      name: '{{AFFECTED_AREA}}',
      description: 'Part of the codebase affected by this bug',
      required: false,
      example: 'Authentication module',
    },
    {
      name: '{{REPRODUCTION_STEPS}}',
      description: 'Steps to reproduce the bug',
      required: false,
      example: '1. Go to login page, 2. Enter valid credentials, 3. Click submit',
    },
    {
      name: '{{EXPECTED_BEHAVIOR}}',
      description: 'What should happen instead',
      required: false,
      example: 'User should be logged in and redirected to dashboard',
    },
  ],

  objectives: [
    // Objective 1: Diagnosis
    {
      description: 'Diagnose and understand {{BUG_DESCRIPTION}}',
      tasks: [
        {
          description: 'Reproduce the bug and confirm symptoms',
          acceptanceCriteria: [
            'Bug successfully reproduced',
            'Symptoms documented',
            'Environment details captured',
          ],
        },
        {
          description: 'Identify root cause through code analysis',
          acceptanceCriteria: [
            'Root cause identified',
            'Affected code paths documented',
            'Related issues identified (if any)',
          ],
          routeTo: 'RECON',
          dependsOn: [0],
        },
        {
          description: 'Assess impact and plan fix strategy',
          acceptanceCriteria: [
            'Impact scope documented',
            'Fix approach determined',
            'Risk of regression assessed',
          ],
          routeTo: 'TACCOM',
          dependsOn: [1],
        },
      ],
    },

    // Objective 2: Fix Implementation
    {
      description: 'Implement fix for {{BUG_DESCRIPTION}}',
      tasks: [
        {
          description: 'Write failing test that reproduces the bug',
          acceptanceCriteria: [
            'Test fails with current code',
            'Test captures the exact bug behavior',
            'Test will pass when bug is fixed',
          ],
          routeTo: 'SENTINEL',
        },
        {
          description: 'Implement the fix',
          acceptanceCriteria: [
            'Bug is fixed',
            'Fix follows existing code patterns',
            'No new issues introduced',
          ],
          routeTo: 'SURGEON',
          dependsOn: [0],
        },
        {
          description: 'Verify fix resolves the issue',
          acceptanceCriteria: [
            'Previously failing test now passes',
            'Manual reproduction confirms fix',
            'No regression in related functionality',
          ],
          dependsOn: [1],
        },
      ],
    },

    // Objective 3: Verification & Hardening
    {
      description: 'Verify fix and prevent regression',
      tasks: [
        {
          description: 'Add regression tests',
          acceptanceCriteria: [
            'Additional edge cases tested',
            'Related scenarios covered',
            'Test coverage improved',
          ],
          routeTo: 'SENTINEL',
        },
        {
          description: 'Run full test suite and verify no regressions',
          acceptanceCriteria: [
            'All existing tests pass',
            'No new warnings or errors',
            'Build succeeds',
          ],
          dependsOn: [0],
        },
      ],
    },
  ],
}

// =============================================================================
// Bugfix Template Variants
// =============================================================================

/**
 * Quick bugfix template for simple, obvious bugs
 */
export const quickBugfixTemplate: MissionTemplate = {
  ...bugfixTemplate,
  name: 'Quick Bug Fix',
  description: 'Template for fixing simple, obvious bugs',
  defaultComplexity: 'low',
  suggestedCouncilMode: 'none',
  tags: ['bugfix', 'quick', 'simple'],

  objectives: [
    {
      description: 'Fix {{BUG_DESCRIPTION}}',
      tasks: [
        {
          description: 'Identify and fix the bug',
          acceptanceCriteria: [
            'Bug is fixed',
            'No regression introduced',
          ],
          routeTo: 'SURGEON',
        },
        {
          description: 'Verify fix and run tests',
          acceptanceCriteria: [
            'Fix verified',
            'Tests pass',
          ],
          dependsOn: [0],
        },
      ],
    },
  ],
}

/**
 * Critical bugfix template for production issues
 */
export const criticalBugfixTemplate: MissionTemplate = {
  ...bugfixTemplate,
  name: 'Critical Bug Fix',
  description: 'Template for fixing critical production bugs with full verification',
  defaultComplexity: 'critical',
  suggestedCouncilMode: 'standard',
  tags: ['bugfix', 'critical', 'production', 'hotfix'],

  variables: [
    ...bugfixTemplate.variables,
    {
      name: '{{SEVERITY}}',
      description: 'Severity level of the bug',
      required: true,
      example: 'P0 - Production down',
    },
    {
      name: '{{AFFECTED_USERS}}',
      description: 'Estimated number of users affected',
      required: false,
      example: 'All users attempting to login',
    },
  ],

  objectives: [
    ...bugfixTemplate.objectives,
    // Additional objective for critical bugs: Post-mortem
    {
      description: 'Document incident and preventive measures',
      tasks: [
        {
          description: 'Write post-mortem documentation',
          acceptanceCriteria: [
            'Timeline documented',
            'Root cause explained',
            'Prevention measures identified',
          ],
          routeTo: 'SCRIBE',
        },
        {
          description: 'Identify and implement preventive measures',
          acceptanceCriteria: [
            'Monitoring added if applicable',
            'Safeguards implemented',
            'Similar issues prevented',
          ],
        },
      ],
    },
  ],
}

/**
 * Security bugfix template for security vulnerabilities
 */
export const securityBugfixTemplate: MissionTemplate = {
  ...criticalBugfixTemplate,
  name: 'Security Bug Fix',
  description: 'Template for fixing security vulnerabilities',
  tags: ['bugfix', 'security', 'vulnerability', 'critical'],

  variables: [
    ...criticalBugfixTemplate.variables,
    {
      name: '{{CVE_ID}}',
      description: 'CVE identifier if applicable',
      required: false,
      example: 'CVE-2024-12345',
    },
    {
      name: '{{VULNERABILITY_TYPE}}',
      description: 'Type of vulnerability (OWASP category)',
      required: false,
      example: 'SQL Injection',
    },
  ],
}
