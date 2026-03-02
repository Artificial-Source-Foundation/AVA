/**
 * XML-based tool call parser for models without native tool_use support.
 * Parses <tool_call> XML from text responses and injects tool schemas into system prompt.
 */

import type { ToolDefinition, ToolUseBlock } from './types.js'

const TOOL_CALL_REGEX =
  /<tool_call>\s*<name>([\s\S]*?)<\/name>\s*<arguments>([\s\S]*?)<\/arguments>\s*<\/tool_call>/g

/**
 * Parse tool calls from XML in text response.
 * Returns extracted tool calls and the remaining text.
 */
export function parseToolCallsFromText(text: string): {
  toolCalls: ToolUseBlock[]
  remainingText: string
} {
  const toolCalls: ToolUseBlock[] = []
  let remainingText = text
  let callIndex = 0

  // Reset regex state
  TOOL_CALL_REGEX.lastIndex = 0

  while (true) {
    const match = TOOL_CALL_REGEX.exec(text)
    if (!match) break

    const name = match[1]!.trim()
    const argsStr = match[2]!.trim()

    try {
      const input = JSON.parse(argsStr) as Record<string, unknown>
      toolCalls.push({
        type: 'tool_use',
        id: `shim-${Date.now()}-${callIndex++}`,
        name,
        input,
      })
    } catch {
      // Skip invalid JSON args
    }

    // Remove the matched tool call from text
    remainingText = remainingText.replace(match[0], '').trim()
  }

  return { toolCalls, remainingText }
}

/**
 * Build XML tool descriptions for injection into system prompt.
 */
export function buildToolSchemaXML(tools: ToolDefinition[]): string {
  const lines: string[] = ['<available_tools>']

  for (const tool of tools) {
    lines.push('  <tool>')
    lines.push(`    <name>${tool.name}</name>`)
    lines.push(`    <description>${escapeXml(tool.description)}</description>`)
    lines.push(`    <parameters>${JSON.stringify(tool.input_schema, null, 2)}</parameters>`)
    lines.push('  </tool>')
  }

  lines.push('</available_tools>')
  lines.push('')
  lines.push('To use a tool, respond with:')
  lines.push('<tool_call>')
  lines.push('  <name>tool_name</name>')
  lines.push('  <arguments>{"param": "value"}</arguments>')
  lines.push('</tool_call>')

  return lines.join('\n')
}

function escapeXml(str: string): string {
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

/**
 * Check if a model needs the tool shim (doesn't support native tool calling).
 */
export function needsToolShim(provider: string, model: string): boolean {
  // Models known to NOT support tool calling
  const shimNeeded = [
    'ollama/', // Most local models via Ollama
    'text-davinci',
    'gpt-3.5-turbo-instruct',
  ]

  const fullId = `${provider}/${model}`
  return shimNeeded.some((prefix) => fullId.startsWith(prefix) || model.startsWith(prefix))
}
