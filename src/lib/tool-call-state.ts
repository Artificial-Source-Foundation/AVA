import type { Message, ToolCall, ToolCallStatus } from '../types'

const TOOL_CALL_STATUSES = new Set<ToolCallStatus>(['pending', 'running', 'success', 'error'])

function asRecord(value: unknown): Record<string, unknown> | null {
  return value && typeof value === 'object' && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : null
}

function asNumber(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined
}

function asString(value: unknown): string | undefined {
  return typeof value === 'string' ? value : undefined
}

function normalizeToolCall(value: unknown): ToolCall | null {
  const record = asRecord(value)
  if (!record) return null

  const id = asString(record.id)
  const name = asString(record.name)
  if (!id || !name) return null

  const args = asRecord(record.args) ?? asRecord(record.arguments) ?? {}
  const explicitStatus = asString(record.status)
  const status = TOOL_CALL_STATUSES.has(explicitStatus as ToolCallStatus)
    ? (explicitStatus as ToolCallStatus)
    : asString(record.error)
      ? 'error'
      : 'success'
  const filePath =
    asString(record.filePath) ??
    asString(args.file_path) ??
    asString(args.filePath) ??
    asString(args.path) ??
    asString(args.output_path)

  return {
    id,
    name,
    args,
    status,
    output: asString(record.output),
    streamingOutput: asString(record.streamingOutput),
    error: asString(record.error),
    startedAt: asNumber(record.startedAt) ?? 0,
    completedAt: asNumber(record.completedAt),
    filePath,
    diff: asRecord(record.diff) as ToolCall['diff'] | undefined,
    uiResource: asRecord(record.uiResource) as ToolCall['uiResource'] | undefined,
    contentOffset: asNumber(record.contentOffset),
    approvalDecision:
      record.approvalDecision === 'once' ||
      record.approvalDecision === 'always' ||
      record.approvalDecision === 'denied'
        ? record.approvalDecision
        : undefined,
  }
}

export function normalizeToolCalls(value: unknown): ToolCall[] | undefined {
  if (!Array.isArray(value) || value.length === 0) return undefined

  const normalized = value
    .map((toolCall) => normalizeToolCall(toolCall))
    .filter((toolCall): toolCall is ToolCall => toolCall !== null)

  return normalized.length > 0 ? normalized : undefined
}

function toolCallDetailScore(toolCall: ToolCall): number {
  let score = 0
  if (Object.keys(toolCall.args).length > 0) score += 1
  if (toolCall.output) score += 2
  if (toolCall.streamingOutput) score += 1
  if (toolCall.error) score += 2
  if (toolCall.completedAt !== undefined) score += 1
  if (toolCall.contentOffset !== undefined) score += 2
  if (toolCall.diff) score += 2
  if (toolCall.uiResource) score += 1
  if (toolCall.approvalDecision) score += 1
  return score
}

function mergeToolCallWithExisting(existing: ToolCall, incoming: ToolCall): ToolCall {
  const incomingHasArgs = Object.keys(incoming.args).length > 0

  return {
    id: incoming.id,
    name: incoming.name,
    args: incomingHasArgs ? incoming.args : existing.args,
    status: incoming.status,
    output: incoming.output ?? existing.output,
    streamingOutput: incoming.streamingOutput ?? existing.streamingOutput,
    error: incoming.error ?? existing.error,
    startedAt: incoming.startedAt > 0 ? incoming.startedAt : existing.startedAt,
    completedAt: incoming.completedAt ?? existing.completedAt,
    filePath: incoming.filePath ?? existing.filePath,
    diff: incoming.diff ?? existing.diff,
    uiResource: incoming.uiResource ?? existing.uiResource,
    contentOffset: incoming.contentOffset ?? existing.contentOffset,
    approvalDecision: incoming.approvalDecision ?? existing.approvalDecision,
  }
}

function chooseToolCalls(
  existingToolCalls: ToolCall[] | undefined,
  incomingToolCalls: ToolCall[] | undefined
): ToolCall[] | undefined {
  if (!existingToolCalls?.length) return incomingToolCalls
  if (!incomingToolCalls?.length) return existingToolCalls

  const existingById = new Map(existingToolCalls.map((toolCall) => [toolCall.id, toolCall]))
  let mergedCount = 0

  const merged = incomingToolCalls.map((incomingToolCall) => {
    const existingToolCall = existingById.get(incomingToolCall.id)
    if (!existingToolCall || existingToolCall.name !== incomingToolCall.name) {
      return incomingToolCall
    }

    if (toolCallDetailScore(existingToolCall) > toolCallDetailScore(incomingToolCall)) {
      mergedCount += 1
      return mergeToolCallWithExisting(existingToolCall, incomingToolCall)
    }

    return incomingToolCall
  })

  return mergedCount > 0 ? merged : incomingToolCalls
}

export function extractToolCallsFromMetadata(
  metadata: Record<string, unknown> | undefined
): ToolCall[] | undefined {
  return normalizeToolCalls(metadata?.toolCalls)
}

export function mergeMessageWithBackend(existing: Message, incoming: Message): Message {
  const toolCalls = chooseToolCalls(existing.toolCalls, incoming.toolCalls)
  const metadata =
    toolCalls && toolCalls !== incoming.toolCalls
      ? {
          ...(incoming.metadata ?? existing.metadata ?? {}),
          toolCalls,
        }
      : (incoming.metadata ?? existing.metadata)

  return {
    ...existing,
    ...incoming,
    metadata,
    toolCalls: toolCalls ?? incoming.toolCalls ?? existing.toolCalls,
  }
}

export function mergeMessagesWithExisting(
  existingMessages: Message[],
  incomingMessages: Message[]
): Message[] {
  if (existingMessages.length === 0 || incomingMessages.length === 0) {
    return incomingMessages
  }

  const existingById = new Map(existingMessages.map((message) => [message.id, message]))
  let mergedCount = 0

  const mergedMessages = incomingMessages.map((incomingMessage) => {
    const existingMessage = existingById.get(incomingMessage.id)
    if (!existingMessage) {
      return incomingMessage
    }

    const mergedMessage = mergeMessageWithBackend(existingMessage, incomingMessage)
    if (mergedMessage !== incomingMessage) {
      mergedCount += 1
    }
    return mergedMessage
  })

  return mergedCount > 0 ? mergedMessages : incomingMessages
}
