/**
 * Generic Prompt Variant
 * Fallback prompts for unknown/untested models
 *
 * Generic characteristics:
 * - Minimal, safe prompts
 * - Avoids advanced features
 * - Works with most models
 */

import type { SystemPromptContext } from '../system.js'
import type { PromptVariant } from './types.js'

// ============================================================================
// Generic Rules (Minimal)
// ============================================================================

const GENERIC_RULES = `
## RULES

1. Working directory: {{CWD}}
2. Use absolute paths
3. Wait for tool results before continuing
4. Read files before editing
5. Call attempt_completion to finish
6. Stay within working directory
`

// ============================================================================
// Generic Capabilities
// ============================================================================

const GENERIC_CAPABILITIES = `
## TOOLS

- glob: Find files
- read: Read files
- grep: Search files
- create: Create files
- write: Write files
- edit: Edit files
- delete: Delete files
- ls: List directories
- bash: Run commands
- attempt_completion: Finish task
`

// ============================================================================
// Generic Variant Implementation
// ============================================================================

export const genericVariant: PromptVariant = {
  family: 'generic',

  getRules(context: SystemPromptContext): string {
    let rules = GENERIC_RULES
    rules = rules.replace(/{{CWD}}/g, context.cwd)
    return rules
  },

  getCapabilities(_context: SystemPromptContext): string {
    return GENERIC_CAPABILITIES
  },

  buildSystemPrompt(context: SystemPromptContext): string {
    const sections: string[] = []

    sections.push(`You are a coding agent. Complete the task using tools.`)

    sections.push(`
Working Directory: ${context.cwd}
OS: ${context.os ?? 'unknown'}
`)

    sections.push(this.getRules(context))
    sections.push(this.getCapabilities(context))

    if (context.customContext) {
      sections.push(`Context: ${context.customContext}`)
    }

    return sections.join('\n')
  },

  buildWorkerPrompt(context: SystemPromptContext): string {
    return `Coding assistant. Complete task.

Working Directory: ${context.cwd}

Rules: Use absolute paths. Call attempt_completion when done.

${context.customContext ?? ''}
`
  },

  getModelNotes(): string {
    return ''
  },
}
