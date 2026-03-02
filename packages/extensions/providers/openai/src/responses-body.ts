/**
 * OpenAI Responses API Request Body Builder
 *
 * Converts ChatMessage[] to the Responses API format used by
 * newer OpenAI models (GPT-5+, o3, o4, Codex).
 *
 * https://platform.openai.com/docs/api-reference/responses
 */

import type { ChatMessage, MessageContent, ProviderConfig, TextBlock } from '@ava/core-v2/llm'

// ─── Types ──────────────────────────────────────────────────────────────────

interface ResponsesInputItem {
  role: string
  content: Array<{ type: string; text: string }>
}

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

// ─── Builder ────────────────────────────────────────────────────────────────

/**
 * Build a Responses API request body from ChatMessages.
 *
 * System messages are extracted into the `instructions` field.
 * User/assistant messages become `input` items with `input_text`/`output_text`.
 * Tools use the flat format (no nested `function` wrapper).
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

  if (tools) body.tools = tools
  if (config.maxTokens) body.max_output_tokens = config.maxTokens
  if (config.temperature !== undefined) body.temperature = config.temperature

  return body
}
