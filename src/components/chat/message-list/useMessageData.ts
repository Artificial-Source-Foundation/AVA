/**
 * Message list computed data hook
 *
 * Derives indexed lookups, visible message windows, and model-change
 * detection from the raw message list.
 */

import { type Accessor, createMemo, createSignal, untrack } from 'solid-js'
import type { Message } from '../../../types'

export interface Checkpoint {
  id: string
  messageCount: number
  description: string
}

export interface UseMessageDataOptions {
  messages: Accessor<Message[]>
  checkpoints: Accessor<Checkpoint[]>
}

export interface MessageDataAPI {
  messageIndexById: Accessor<Map<string, number>>
  checkpointAtIndex: (msgIndex: number) => { id: string; description: string } | null
  modelChangeById: Accessor<Map<string, { from: string; to: string }>>
  visibleMessages: Accessor<Message[]>
  hiddenMessageCount: Accessor<number>
  lastMessageId: Accessor<string | null>
  visibleLimit: Accessor<number>
  loadOlderMessages: () => void
}

export function useMessageData(opts: UseMessageDataOptions): MessageDataAPI {
  const adaptiveChunk = () => Math.max(50, Math.min(300, Math.floor(window.innerHeight / 60)))
  const [visibleLimit, setVisibleLimit] = createSignal(adaptiveChunk())

  const messageCount = createMemo(() => opts.messages().length)

  const messageIndexById = createMemo(() => {
    messageCount() // tracked: only re-run when count changes
    return untrack(() => {
      const indexMap = new Map<string, number>()
      const msgs = opts.messages()
      for (let i = 0; i < msgs.length; i++) {
        indexMap.set(msgs[i].id, i)
      }
      return indexMap
    })
  })

  const checkpointByIndex = createMemo(() => {
    const map = new Map<number, { id: string; description: string }>()
    for (const c of opts.checkpoints()) {
      map.set(c.messageCount - 1, { id: c.id, description: c.description })
    }
    return map
  })

  const modelChangeById = createMemo(() => {
    const map = new Map<string, { from: string; to: string }>()
    let lastAssistantModel = ''

    for (const msg of opts.messages()) {
      if (msg.role !== 'assistant') continue

      const currentModel = (msg.metadata?.model as string) || msg.model || ''
      if (!currentModel) continue

      if (lastAssistantModel && lastAssistantModel !== currentModel) {
        map.set(msg.id, { from: lastAssistantModel, to: currentModel })
      }

      lastAssistantModel = currentModel
    }

    return map
  })

  const checkpointAtIndex = (msgIndex: number): { id: string; description: string } | null =>
    checkpointByIndex().get(msgIndex) ?? null

  const visibleMessages = createMemo(() => {
    const all = opts.messages()
    const limit = visibleLimit()
    if (all.length <= limit) return all
    return all.slice(-limit)
  })

  const hiddenMessageCount = createMemo(() =>
    Math.max(0, opts.messages().length - visibleMessages().length)
  )

  const lastMessageId = createMemo(() => {
    const msgs = opts.messages()
    if (msgs.length === 0) return null
    // If the last messages are mid-stream tier messages (steering/follow-up/post-complete),
    // the streaming assistant message before them should still be considered "last"
    // so that isActiveStreaming stays true and the Working card stays visible.
    const last = msgs[msgs.length - 1]
    if (last.role === 'user' && last.metadata?.tier) {
      // Walk backwards to find the assistant message being streamed
      for (let i = msgs.length - 2; i >= 0; i--) {
        if (msgs[i].role === 'assistant') return msgs[i].id
        if (msgs[i].role === 'user' && !msgs[i].metadata?.tier) break
      }
    }
    return last.id
  })

  const loadOlderMessages = (): void => {
    const increment = Math.max(100, Math.min(400, Math.floor(window.innerHeight / 60) * 2))
    setVisibleLimit((limit) => limit + increment)
  }

  return {
    messageIndexById,
    checkpointAtIndex,
    modelChangeById,
    visibleMessages,
    hiddenMessageCount,
    lastMessageId,
    visibleLimit,
    loadOlderMessages,
  }
}
