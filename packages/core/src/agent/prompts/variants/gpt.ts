/**
 * GPT Prompt Variant
 * Optimized prompts for GPT models (OpenAI)
 *
 * GPT characteristics:
 * - Works well with concise, direct instructions
 * - Native function calling support
 * - Good at following step-by-step reasoning
 * - Prefers markdown formatting
 */

import type { SystemPromptContext } from '../system.js'
import type { PromptVariant } from './types.js'

// ============================================================================
// GPT-Specific Rules (More Concise)
// ============================================================================

const GPT_RULES = `
## RULES

### Environment
- Working directory: {{CWD}}
- Use absolute paths. Avoid ~ or $HOME.
- OS: {{OS}}

### Tools
- Wait for results before proceeding
- Handle errors by trying alternatives
- Read files before editing
- Use glob/grep for discovery

### Code
- Minimal, focused changes only
- Match existing patterns
- Don't add comments/annotations to unchanged code

### Completion
- MUST call \`attempt_completion\` to finish
- Include results in the \`result\` parameter
- Verify changes work before completing

### Safety
- No secrets in output
- Stay within working directory
- Use requires_approval for risky operations
`

// ============================================================================
// GPT-Specific Capabilities
// ============================================================================

const GPT_CAPABILITIES = `
## TOOLS

**Files:** glob, read, grep, create, write, edit, delete, ls
**Commands:** bash
**Tasks:** todoread, todowrite, task, attempt_completion
**Communication:** question
**Web:** websearch, webfetch
**Browser:** browser (launch, click, type, scroll)
`

// ============================================================================
// GPT Variant Implementation
// ============================================================================

export const gptVariant: PromptVariant = {
  family: 'gpt',

  getRules(context: SystemPromptContext): string {
    let rules = GPT_RULES
    rules = rules.replace(/{{CWD}}/g, context.cwd)
    rules = rules.replace(/{{OS}}/g, context.os ?? 'unknown')
    return rules
  },

  getCapabilities(_context: SystemPromptContext): string {
    return GPT_CAPABILITIES
  },

  buildSystemPrompt(context: SystemPromptContext): string {
    const sections: string[] = []

    // Concise role
    sections.push(
      `You are an autonomous coding agent. Complete tasks using tools. No user input available.`
    )

    // Environment
    sections.push(`
## ENVIRONMENT
- Working Directory: ${context.cwd}
- OS: ${context.os ?? 'unknown'}
- Shell: ${context.shell ?? 'bash'}
- Date: ${new Date().toLocaleDateString()}
`)

    // Rules
    sections.push(this.getRules(context))

    // Capabilities
    sections.push(this.getCapabilities(context))

    // Best practices (condensed)
    sections.push(`
## WORKFLOW
1. Discover: glob, grep to understand codebase
2. Read: Examine relevant files
3. Plan: Determine minimal changes
4. Execute: Make changes one at a time
5. Verify: Test/lint changes
6. Complete: Call attempt_completion
`)

    // Model notes
    sections.push(this.getModelNotes())

    // Custom context
    if (context.customContext) {
      sections.push(`
## CONTEXT
${context.customContext}
`)
    }

    return sections.join('\n')
  },

  buildWorkerPrompt(context: SystemPromptContext): string {
    return `Focused coding assistant. Complete task using tools.

**CWD:** ${context.cwd}
**OS:** ${context.os ?? 'unknown'}

Rules: Use absolute paths. Wait for results. Call attempt_completion when done.

${context.customContext ?? ''}
`
  },

  getModelNotes(): string {
    return `
## GPT Notes
- Use markdown for formatting
- Break complex tasks into steps
- Verify assumptions explicitly
- Keep responses focused
`
  },
}
