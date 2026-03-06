import { EventStore } from '@ava/core-v2'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

type EventPayload = {
  sessionId?: string
  agentId?: string
  correlationId?: string
} & Record<string, unknown>

const AGENT_EVENTS = [
  'agent:start',
  'agent:message',
  'agent:tool:call',
  'agent:tool:result',
  'agent:tool:error',
  'agent:error',
  'agent:cancelled',
  'agent:finish',
] as const

export function activateEventStore(api: ExtensionAPI): Disposable {
  const store = new EventStore()
  const disposables = AGENT_EVENTS.map((eventName) =>
    api.on(eventName, (data) => {
      const payload = data as EventPayload
      const { sessionId, agentId, correlationId, ...eventPayload } = payload

      store.append({
        sessionId: sessionId ?? agentId ?? 'global',
        type: eventName,
        payload: eventPayload,
        parentEventId: correlationId,
      })
    })
  )

  return {
    dispose() {
      for (const disposable of disposables) {
        disposable.dispose()
      }
    },
  }
}
