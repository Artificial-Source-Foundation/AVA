/**
 * OpenAI Responses API Request Body Builder
 *
 * Converts ChatMessage[] to the Responses API format used by
 * newer OpenAI models (GPT-5+, o3, o4, Codex).
 *
 * https://platform.openai.com/docs/api-reference/responses
 */

import type {
  ChatMessage,
  ContentBlock,
  MessageContent,
  ProviderConfig,
  TextBlock,
  ToolResultBlock,
  ToolUseBlock,
} from '@ava/core-v2/llm'

// ─── Types ──────────────────────────────────────────────────────────────────

/** Text message item (user or assistant). */
interface ResponsesTextItem {
  role: string
  content: Array<{ type: string; text: string }>
}

/** Function call item — emitted by assistant when calling a tool. */
interface ResponsesFunctionCallItem {
  type: 'function_call'
  call_id: string
  name: string
  arguments: string
}

/** Function call output — the result returned to the model. */
interface ResponsesFunctionCallOutputItem {
  type: 'function_call_output'
  call_id: string
  output: string
}

type ResponsesInputItem =
  | ResponsesTextItem
  | ResponsesFunctionCallItem
  | ResponsesFunctionCallOutputItem

interface ResponsesTool {
  type: 'function'
  name: string
  description: string
  parameters: unknown
}

interface ResponsesRequestBody {
  model: string
  instructions: string
  input: ResponsesInputItem[]
  tools?: ResponsesTool[]
  tool_choice?: string
  store: boolean
  stream: boolean
  max_output_tokens?: number
  temperature?: number
}

// ─── Helpers ────────────────────────────────────────────────────────────────

/** Extract plain text from message content (handles strings and block arrays). */
function extractText(content: MessageContent): string {
  if (typeof content === 'string') return content
  return content
    .filter((b): b is TextBlock => b.type === 'text')
    .map((b) => b.text)
    .join('\n')
}

/** Check if content blocks contain tool_use or tool_result entries. */
function hasToolBlocks(content: MessageContent): boolean {
  if (typeof content === 'string') return false
  return content.some((b) => b.type === 'tool_use' || b.type === 'tool_result')
}

/** Extract ToolUseBlock entries from content blocks. */
function getToolUseBlocks(content: ContentBlock[]): ToolUseBlock[] {
  return content.filter((b): b is ToolUseBlock => b.type === 'tool_use')
}

/** Extract ToolResultBlock entries from content blocks. */
function getToolResultBlocks(content: ContentBlock[]): ToolResultBlock[] {
  return content.filter((b): b is ToolResultBlock => b.type === 'tool_result')
}

// ─── Builder ────────────────────────────────────────────────────────────────

/**
 * Build a Responses API request body from ChatMessages.
 *
 * System messages are extracted into the `instructions` field.
 * User/assistant text → `input_text`/`output_text` items.
 * ToolUseBlock → `function_call` items.
 * ToolResultBlock → `function_call_output` items.
 */
export function buildResponsesRequestBody(
  messages: ChatMessage[],
  config: ProviderConfig,
  instructions?: string
): ResponsesRequestBody {
  // Collect system instructions
  const systemParts: string[] = []
  if (instructions) systemParts.push(instructions)

  for (const msg of messages) {
    if (msg.role === 'system') {
      systemParts.push(extractText(msg.content))
    }
  }

  // Convert non-system messages to input items
  const input: ResponsesInputItem[] = []
  for (const msg of messages) {
    if (msg.role === 'system') continue

    // Assistant message with tool calls → emit text + function_call items
    if (msg.role === 'assistant' && typeof msg.content !== 'string' && hasToolBlocks(msg.content)) {
      const text = extractText(msg.content)
      if (text) {
        input.push({
          role: 'assistant',
          content: [{ type: 'output_text', text }],
        })
      }
      for (const tc of getToolUseBlocks(msg.content)) {
        input.push({
          type: 'function_call',
          call_id: tc.id,
          name: tc.name,
          arguments: JSON.stringify(tc.input),
        })
      }
      continue
    }

    // User message with tool results → emit function_call_output items
    if (msg.role === 'user' && typeof msg.content !== 'string' && hasToolBlocks(msg.content)) {
      for (const tr of getToolResultBlocks(msg.content)) {
        input.push({
          type: 'function_call_output',
          call_id: tr.tool_use_id,
          output: tr.content,
        })
      }
      continue
    }

    // Plain text message
    const text = extractText(msg.content)
    if (!text) continue

    const contentType = msg.role === 'assistant' ? 'output_text' : 'input_text'
    input.push({
      role: msg.role,
      content: [{ type: contentType, text }],
    })
  }

  // Convert tools to flat Responses API format
  const tools: ResponsesTool[] | undefined = config.tools?.length
    ? config.tools.map((t) => ({
        type: 'function' as const,
        name: t.name,
        description: t.description,
        parameters: t.input_schema,
      }))
    : undefined

  const body: ResponsesRequestBody = {
    model: config.model,
    instructions: systemParts.join('\n\n') || 'You are AVA, a coding assistant.',
    input,
    store: false,
    stream: true,
  }

  if (tools) {
    body.tools = tools
    // Pass tool_choice — Responses API supports "auto" | "required" | "none"
    if (config.toolChoice) {
      body.tool_choice = config.toolChoice.type === 'tool' ? 'required' : config.toolChoice.type
    }
  }
  if (config.maxTokens) body.max_output_tokens = config.maxTokens
  if (config.temperature !== undefined) body.temperature = config.temperature

  return body
}
