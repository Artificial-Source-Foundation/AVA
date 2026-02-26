/**
 * Event Logger Plugin
 *
 * Demonstrates: api.on(), emit(), events + storage, registerCommand()
 * Logs all agent events to storage and provides /events to view them.
 */

import type { Disposable, ExtensionAPI, SlashCommand } from '@ava/core-v2/extensions'

interface LogEntry {
  event: string
  timestamp: number
  data: unknown
}

const TRACKED_EVENTS = [
  'agent:turn:start',
  'agent:turn:end',
  'agent:completing',
  'tool:before',
  'tool:after',
]

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  const maxEntries = 100

  async function logEvent(event: string, data: unknown): Promise<void> {
    const entries = (await api.storage.get<LogEntry[]>('event-log')) ?? []
    entries.push({ event, timestamp: Date.now(), data })
    // Keep only recent entries
    if (entries.length > maxEntries) {
      entries.splice(0, entries.length - maxEntries)
    }
    await api.storage.set('event-log', entries)
  }

  // Subscribe to all tracked events
  for (const eventName of TRACKED_EVENTS) {
    disposables.push(
      api.on(eventName, (data) => {
        logEvent(eventName, data)
      })
    )
  }

  // Register /events command
  const eventsCommand: SlashCommand = {
    name: 'events',
    description: 'View logged agent events. Usage: /events [count] [clear]',

    async execute(args, _ctx) {
      const trimmed = args.trim()

      if (trimmed === 'clear') {
        await api.storage.delete('event-log')
        return 'Event log cleared.'
      }

      const entries = (await api.storage.get<LogEntry[]>('event-log')) ?? []
      if (entries.length === 0) return 'No events logged yet.'

      const count = parseInt(trimmed, 10) || 20
      const recent = entries.slice(-count)
      return recent
        .map((e) => {
          const time = new Date(e.timestamp).toISOString()
          return `[${time}] ${e.event}`
        })
        .join('\n')
    },
  }

  disposables.push(api.registerCommand(eventsCommand))
  api.log.info('Event logger activated')

  return {
    dispose() {
      for (const d of disposables.reverse()) {
        d.dispose()
      }
    },
  }
}
