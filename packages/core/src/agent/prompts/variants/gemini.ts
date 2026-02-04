/**
 * Gemini Prompt Variant
 * Optimized prompts for Gemini models (Google)
 *
 * Gemini characteristics:
 * - Good multimodal capabilities
 * - Handles structured prompts well
 * - May have formatting quirks (literal \n in output)
 * - Benefits from explicit reasoning steps
 */

import type { SystemPromptContext } from '../system.js'
import type { PromptVariant } from './types.js'

// ============================================================================
// Gemini-Specific Rules
// ============================================================================

const GEMINI_RULES = `
## RULES

### Environment
- Working directory: {{CWD}}
- OS: {{OS}}
- Use absolute paths for all file operations

### Tool Usage
- Call tools one at a time
- Wait for each result before continuing
- Read files before making edits
- Use glob and grep to explore the codebase

### Code Changes
- Make minimal, focused changes
- Follow existing code patterns
- Don't modify code you don't need to change

### Task Completion
- Call attempt_completion when done
- Include your results in the result parameter
- Verify your changes work first

### Safety
- Stay within the working directory
- Don't expose secrets or credentials
- Use requires_approval for dangerous operations
`

// ============================================================================
// Gemini-Specific Capabilities
// ============================================================================

const GEMINI_CAPABILITIES = `
## AVAILABLE TOOLS

### File Operations
glob - Find files by pattern
read - Read file contents
grep - Search file contents
create - Create new files
write - Write/overwrite files
edit - Modify parts of a file
delete - Remove files
ls - List directory contents

### Commands
bash - Execute shell commands

### Tasks
todoread - View task list
todowrite - Update task list
task - Spawn subagent
attempt_completion - Finish task

### Other
question - Ask user (use sparingly)
websearch - Web search
webfetch - Fetch web pages
browser - Browser automation
`

// ============================================================================
// Gemini Variant Implementation
// ============================================================================

export const geminiVariant: PromptVariant = {
  family: 'gemini',

  getRules(context: SystemPromptContext): string {
    let rules = GEMINI_RULES
    rules = rules.replace(/{{CWD}}/g, context.cwd)
    rules = rules.replace(/{{OS}}/g, context.os ?? 'unknown')
    return rules
  },

  getCapabilities(_context: SystemPromptContext): string {
    return GEMINI_CAPABILITIES
  },

  buildSystemPrompt(context: SystemPromptContext): string {
    const sections: string[] = []

    // Clear role statement
    sections.push(`ROLE: Autonomous coding agent
TASK: Complete user request using tools
MODE: Autonomous (no user input during execution)`)

    // Environment
    sections.push(`
## ENVIRONMENT

Working Directory: ${context.cwd}
Operating System: ${context.os ?? 'unknown'}
Shell: ${context.shell ?? 'bash'}
Date: ${new Date().toLocaleDateString()}
`)

    // Rules
    sections.push(this.getRules(context))

    // Capabilities
    sections.push(this.getCapabilities(context))

    // Workflow
    sections.push(`
## WORKFLOW

Step 1: Use glob and grep to understand the codebase
Step 2: Read relevant files
Step 3: Plan your changes
Step 4: Execute changes one at a time
Step 5: Verify changes work (run tests, lint)
Step 6: Call attempt_completion with results
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
    return `ROLE: Coding assistant
TASK: Complete assigned task

Working Directory: ${context.cwd}
OS: ${context.os ?? 'unknown'}

RULES:
1. Use absolute paths
2. Wait for tool results
3. Call attempt_completion when done

${context.customContext ?? ''}
`
  },

  getModelNotes(): string {
    return `
## GEMINI NOTES

- Use clear, structured formatting
- Be explicit about reasoning steps
- Double-check output for formatting issues
- Use numbered lists for sequences
`
  },
}
