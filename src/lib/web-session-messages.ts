import type { Message } from '../types'
import { extractToolCallsFromMetadata } from './tool-call-state'

type RawWebSessionMessage = Record<string, unknown>

function parseMessageImages(rawImages: unknown): Message['images'] | undefined {
  if (!Array.isArray(rawImages) || rawImages.length === 0) {
    return undefined
  }

  const images = rawImages
    .map((image) => {
      if (!image || typeof image !== 'object') {
        return null
      }

      const record = image as Record<string, unknown>
      const data = typeof record.data === 'string' ? record.data : undefined
      const mimeType =
        typeof record.mimeType === 'string'
          ? record.mimeType
          : typeof record.mediaType === 'string'
            ? record.mediaType
            : typeof record.media_type === 'string'
              ? record.media_type
              : undefined

      return data && mimeType ? { data, mimeType } : null
    })
    .filter((image): image is NonNullable<typeof image> => image !== null)

  return images.length > 0 ? images : undefined
}

function parseMetadataValue(rawMetadata: unknown): Record<string, unknown> | undefined {
  if (typeof rawMetadata === 'string' && rawMetadata.trim().length > 0) {
    try {
      const parsed = JSON.parse(rawMetadata) as Record<string, unknown>
      return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed : undefined
    } catch {
      return undefined
    }
  }

  return rawMetadata && typeof rawMetadata === 'object' && !Array.isArray(rawMetadata)
    ? (rawMetadata as Record<string, unknown>)
    : undefined
}

function parseCreatedAt(message: RawWebSessionMessage): number {
  return typeof message.created_at === 'number'
    ? message.created_at
    : typeof message.timestamp === 'string'
      ? new Date(message.timestamp).getTime()
      : typeof message.created_at === 'string'
        ? new Date(message.created_at).getTime()
        : Date.now()
}

export function buildWebSessionMessageMetadata(
  message: RawWebSessionMessage
): Record<string, unknown> | undefined {
  const metadata = parseMetadataValue(message.metadata)
  const toolCalls = Array.isArray(message.tool_calls) ? message.tool_calls : undefined
  const images = parseMessageImages(message.images)

  if (toolCalls && toolCalls.length > 0) {
    return {
      ...(metadata ?? {}),
      toolCalls,
      ...(images ? { images } : {}),
    }
  }

  if (images) {
    return {
      ...(metadata ?? {}),
      images,
    }
  }

  return metadata
}

export function mapWebSessionMessages(
  rawMessages: RawWebSessionMessage[],
  sessionId: string
): Message[] {
  return rawMessages.map((message) => {
    const metadata = buildWebSessionMessageMetadata(message)
    const images = parseMessageImages(message.images)

    return {
      id: message.id as string,
      sessionId,
      role: message.role as Message['role'],
      content: (message.content as string) ?? '',
      createdAt: parseCreatedAt(message),
      tokensUsed: (message.tokens_used as number) || undefined,
      costUSD: (message.cost_usd as number | null) ?? undefined,
      model: (message.model as string | null) ?? undefined,
      metadata,
      images,
      toolCalls: extractToolCallsFromMetadata(metadata),
    }
  })
}

export function mapWebSessionMessageRows(
  rawMessages: RawWebSessionMessage[],
  sessionId: string
): Array<Record<string, unknown>> {
  return rawMessages.map((message) => {
    const metadata = buildWebSessionMessageMetadata(message)

    return {
      id: message.id,
      session_id: sessionId,
      role: message.role,
      content: message.content,
      agent_id: message.agent_id ?? null,
      created_at: parseCreatedAt(message),
      tokens_used: message.tokens_used ?? 0,
      cost_usd: message.cost_usd ?? null,
      model: message.model ?? null,
      metadata: JSON.stringify(metadata ?? {}),
    }
  })
}
