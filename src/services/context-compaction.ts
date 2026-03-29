import { generateMessageId } from '../lib/ids'
import { useSession } from '../stores/session'
import { type AppSettings, useSettings } from '../stores/settings'
import type { Message } from '../types'
import type { CompactContextResult } from '../types/rust-ipc'
import { getCoreBudget } from './core-bridge'
import { rustBackend } from './rust-bridge'

export const SAME_AS_CHAT_MODEL = ''
const MODEL_DELIMITER = '::'

export interface CompactionModelOption {
  value: string
  label: string
}

export function encodeCompactionModel(provider: string, model: string): string {
  return `${provider}${MODEL_DELIMITER}${model}`
}

export function decodeCompactionModel(value: string): { provider: string; model: string } | null {
  const [provider, model] = value.split(MODEL_DELIMITER)
  if (!provider || !model) return null
  return { provider, model }
}

export function getCompactionModelOptions(settings: AppSettings): CompactionModelOption[] {
  const seen = new Set<string>()
  const options: CompactionModelOption[] = [
    { value: SAME_AS_CHAT_MODEL, label: 'Same as chat model' },
  ]

  for (const provider of settings.providers) {
    for (const model of provider.models) {
      const value = encodeCompactionModel(provider.id, model.id)
      if (seen.has(value)) continue
      seen.add(value)
      options.push({
        value,
        label: `${provider.name} - ${model.name || model.id}`,
      })
    }
  }

  return options
}

export function parseCompactFocus(raw: string): string | undefined {
  const trimmed = raw.trim()
  if (!trimmed) return undefined
  if (trimmed.toLowerCase().startsWith('focus:')) {
    const focus = trimmed.slice('focus:'.length).trim()
    return focus || undefined
  }
  return trimmed
}

export async function requestConversationCompaction(focus?: string): Promise<CompactContextResult> {
  const session = useSession()
  const { settings } = useSettings()
  const currentSession = session.currentSession()
  const selected = decodeCompactionModel(settings().generation.compactionModel)

  if (!currentSession) {
    throw new Error('No active session to compact')
  }

  return rustBackend.compactContext(
    session.messages().map((message) => ({ role: message.role, content: message.content })),
    focus,
    session.contextUsage().total,
    currentSession.id,
    selected?.provider,
    selected?.model
  )
}

function updateContextBudget(result: CompactContextResult): void {
  const budget = getCoreBudget()
  if (!budget) return

  budget.clear()
  result.messages.forEach((message, index) => {
    budget.addMessage(`compact-${Date.now()}-${index}`, message.content)
  })
  window.dispatchEvent(
    new CustomEvent('ava:core-settings-changed', { detail: { category: 'context' } })
  )
}

function buildSummaryMessage(
  sessionId: string,
  result: CompactContextResult,
  source: 'manual' | 'auto'
): Message {
  return {
    id: generateMessageId('sys'),
    sessionId,
    role: 'system',
    content: result.summary,
    createdAt: Date.now(),
    metadata: {
      system: true,
      contextSummary: {
        source,
        summary: result.contextSummary,
        tokensBefore: result.tokensBefore,
        tokensAfter: result.tokensAfter,
        tokensSaved: result.tokensSaved,
        messagesBefore: result.messagesBefore,
        messagesAfter: result.messagesAfter,
        usageBeforePercent: result.usageBeforePercent,
      },
    },
  }
}

export function applyCompactionResult(
  result: CompactContextResult,
  source: 'manual' | 'auto',
  options: { appendSummaryMessage?: boolean } = {}
): void {
  const session = useSession()
  const currentSession = session.currentSession()
  if (!currentSession) return

  updateContextBudget(result)

  if (options.appendSummaryMessage ?? source === 'manual') {
    session.addMessage(buildSummaryMessage(currentSession.id, result, source))
  }

  window.dispatchEvent(
    new CustomEvent('ava:compacted', {
      detail: {
        source,
        removed: result.messagesBefore - result.messagesAfter,
        tokensSaved: result.tokensSaved,
        usageBeforePercent: result.usageBeforePercent,
      },
    })
  )
}
