import type { AgentMode, Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ToolDefinition } from '@ava/core-v2/llm'

const WINDOWED_TOOLS = new Set([
  'read_file',
  'view_window',
  'scroll_up',
  'scroll_down',
  'goto_line',
  'edit',
  'write_file',
  'apply_patch',
  'glob',
  'grep',
])

const WINDOWED_GUIDANCE = [
  'Windowed Mode is active.',
  'For files larger than 200 lines, use `view_window` then `scroll_up`, `scroll_down`, and `goto_line` to navigate.',
  'After edits, refresh the window with `view_window` to confirm nearby context.',
].join('\n')

export const windowedAgentMode: AgentMode = {
  name: 'windowed',
  description: 'Navigate and edit large files in 100-line windows.',
  filterTools(tools: ToolDefinition[]): ToolDefinition[] {
    return tools.filter((tool) => WINDOWED_TOOLS.has(tool.name))
  },
  systemPrompt(base: string): string {
    return `${base}\n\n${WINDOWED_GUIDANCE}`
  },
}

export function registerWindowedMode(api: ExtensionAPI): Disposable {
  return api.registerAgentMode(windowedAgentMode)
}
