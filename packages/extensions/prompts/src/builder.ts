/**
 * System prompt builder.
 * Constructs the system prompt from sections and model-specific variants.
 */

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

  // Model-specific adjustments
  if (model?.includes('claude')) {
    prompt = `${prompt}\n\nYou are AVA, an AI coding assistant.`
  } else if (model?.includes('gpt')) {
    prompt = `${prompt}\n\nYou are AVA, an AI coding assistant. Be concise and precise.`
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

const CORE_IDENTITY = `You are AVA, an AI coding assistant. You help developers with software engineering tasks including:
- Writing, editing, and debugging code
- Explaining code and architecture
- Running tests and analyzing results
- File system operations and project management

Always verify your changes work before considering a task complete.`

const TOOL_GUIDELINES = `When using tools:
- Read files before modifying them
- Use the edit tool for targeted changes (not write_file for existing files)
- Verify changes by reading the modified file
- Use batch tool for independent parallel operations`

addPromptSection({ name: 'identity', priority: 0, content: CORE_IDENTITY })
addPromptSection({ name: 'tool-guidelines', priority: 10, content: TOOL_GUIDELINES })
