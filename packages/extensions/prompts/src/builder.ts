/**
 * System prompt builder.
 * Constructs the system prompt from sections and model-specific variants.
 */

import { getModelFamilyPromptSection } from './families.js'

export interface PromptSection {
  name: string
  priority: number
  content: string
}

const sections: PromptSection[] = []

export function addPromptSection(section: PromptSection): () => void {
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
    const familySection = getModelFamilyPromptSection(model)
    if (familySection) {
      prompt = `${prompt}\n\n${familySection}`
    }
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

You MUST keep working until the task is completely resolved. NEVER end your turn without having fully solved the problem. Do NOT ask for permission before using a tool — the interface handles confirmation if needed.

When given a task:
1. Understand the request (read relevant files if needed)
2. Implement the solution using your tools
3. Verify your changes work
4. Only then report back to the user

If a task is ambiguous, pick the most reasonable interpretation and execute. Do the work without asking clarifying questions unless truly critical information is missing.`

const TOOL_GUIDELINES = `When using tools:
- Read files before modifying them
- Use the edit tool for targeted changes (not write_file for existing files)
- Verify changes by reading the modified file
- Use batch tool for independent parallel operations
- When you say you will use a tool, ACTUALLY call it — do not just describe what you would do`

addPromptSection({ name: 'identity', priority: 0, content: CORE_IDENTITY })
addPromptSection({ name: 'tool-guidelines', priority: 10, content: TOOL_GUIDELINES })
