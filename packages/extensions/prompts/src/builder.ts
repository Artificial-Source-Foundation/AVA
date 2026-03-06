/**
 * System prompt builder.
 * Constructs the system prompt from sections and model-specific variants.
 */

import { getModelVariantPromptSection } from './model-variants.js'

export interface PromptSection {
  name: string
  priority: number
  content: string
}

const sections: PromptSection[] = []

export function addPromptSection(section: PromptSection): () => void {
  // Deduplicate by name — replace existing section with same name
  const existing = sections.findIndex((s) => s.name === section.name)
  if (existing !== -1) sections.splice(existing, 1)
  sections.push(section)
  sections.sort((a, b) => a.priority - b.priority)
  return () => {
    const idx = sections.indexOf(section)
    if (idx !== -1) sections.splice(idx, 1)
  }
}

export function buildSystemPrompt(model?: string): string {
  const parts: string[] = []

  for (const section of sections) {
    parts.push(section.content)
  }

  let prompt = parts.join('\n\n')

  // Model-family-specific adjustments
  if (model) {
    prompt = `${prompt}\n\n${getModelVariantPromptSection(model)}`
  }

  return prompt
}

export function getPromptSections(): readonly PromptSection[] {
  return [...sections]
}

export function resetPromptSections(): void {
  sections.length = 0
}

// ─── Default Sections ───────────────────────────────────────────────────────

const CORE_IDENTITY = `You are AVA, an autonomous AI coding agent. You solve software engineering tasks by using your tools directly — reading files, writing code, running commands, and verifying results.

You are an agent — keep going until the user's task is completely resolved before ending your turn. Do NOT ask for permission before using a tool — the interface handles confirmation if needed. Do NOT ask the user to run commands manually — you have direct tool access.

When given a task:
1. Understand the request (read relevant files if needed)
2. Implement the solution using your tools directly
3. Verify your changes work
4. Only then report back to the user

Default: do the work without asking questions. Treat short tasks as sufficient direction; infer missing details by reading the codebase and following existing conventions. Only ask when truly blocked after checking relevant context AND you cannot safely pick a reasonable default.`

const TOOL_GUIDELINES = `When using tools:
- Read files before modifying them
- Use the edit tool for targeted changes (not write_file for existing files)
- Verify changes by reading the modified file
- Use batch tool for independent parallel operations
- When you say you will use a tool, ACTUALLY call it — do not just describe what you would do
- NEVER say "I can help with that" or describe actions without doing them — just do it
- You have persistent memory. At the start of complex tasks, use memory_list to check for relevant stored context
- Use memory_write to save important decisions, patterns, or user preferences for future sessions
- When a task requires significant research or analysis before implementation, use plan_enter to switch to read-only mode
- Use plan_exit when ready to implement`

addPromptSection({ name: 'identity', priority: 0, content: CORE_IDENTITY })
addPromptSection({ name: 'tool-guidelines', priority: 10, content: TOOL_GUIDELINES })
