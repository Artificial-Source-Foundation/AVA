import type { AgentEvent } from '@ava/core-v2/agent'
import { AlertTriangle, Brain, Clock3, Download, Wrench } from 'lucide-solid'
import { createMemo, createSignal, For, type JSX, onCleanup, Show } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'

interface TrajectoryInspectorProps {
  sessionId: string
}

interface TimelineEvent {
  id: string
  timestamp: number
  event: AgentEvent
}

const ROW_HEIGHT = 84
const WINDOW_SIZE = 80

function normalizeEvents(events: AgentEvent[]): TimelineEvent[] {
  return events.map((event, idx) => {
    const rawTs = (event as { timestamp?: number }).timestamp
    return {
      id: `${event.type}-${idx}`,
      timestamp: typeof rawTs === 'number' ? rawTs : Date.now() + idx,
      event,
    }
  })
}

function eventLabel(event: AgentEvent): string {
  if (event.type === 'turn:start' || event.type === 'turn:end') {
    const turn = (event as { turn?: number }).turn
    return `Turn ${turn ?? '?'} ${event.type.endsWith('start') ? 'Start' : 'End'}`
  }
  if (event.type === 'tool:start' || event.type === 'tool:finish') {
    const tool = (event as { toolName?: string }).toolName
    return `${event.type.endsWith('start') ? 'Tool Start' : 'Tool Finish'}: ${tool ?? 'unknown'}`
  }
  return event.type
}

function eventTone(type: string): string {
  if (type.includes('error')) return 'border-[var(--error)]/50 bg-[var(--error-subtle)]'
  if (type.includes('doom-loop') || type.includes('stuck')) {
    return 'border-[var(--warning)]/50 bg-[var(--warning-subtle)]'
  }
  if (type.includes('tool:')) return 'border-[var(--accent)]/35 bg-[var(--accent-subtle)]'
  if (type.includes('thought') || type.includes('thinking')) {
    return 'border-[var(--info)]/35 bg-[var(--surface-sunken)]'
  }
  return 'border-[var(--border-subtle)] bg-[var(--surface-raised)]'
}

export function TrajectoryInspector(props: TrajectoryInspectorProps): JSX.Element {
  const agent = useAgent()
  const [eventTypeFilter, setEventTypeFilter] = createSignal('all')
  const [agentFilter, setAgentFilter] = createSignal('')
  const [startMs, setStartMs] = createSignal<number | null>(null)
  const [endMs, setEndMs] = createSignal<number | null>(null)
  const [expanded, setExpanded] = createSignal<Set<string>>(new Set())
  const [scrollTop, setScrollTop] = createSignal(0)

  const timeline = createMemo(() => normalizeEvents(agent.eventTimeline()))
  const eventTypes = createMemo(() => ['all', ...new Set(timeline().map((e) => e.event.type))])

  const filtered = createMemo(() => {
    const type = eventTypeFilter()
    const agentId = agentFilter().trim().toLowerCase()
    const min = startMs()
    const max = endMs()

    return timeline().filter((entry) => {
      const rawAgent = (entry.event as { agentId?: string }).agentId ?? ''
      const matchType = type === 'all' || entry.event.type === type
      const matchAgent = !agentId || rawAgent.toLowerCase().includes(agentId)
      const matchStart = min === null || entry.timestamp >= min
      const matchEnd = max === null || entry.timestamp <= max
      return matchType && matchAgent && matchStart && matchEnd
    })
  })

  const windowed = createMemo(() => {
    const all = filtered()
    const start = Math.max(0, Math.floor(scrollTop() / ROW_HEIGHT) - 10)
    const end = Math.min(all.length, start + WINDOW_SIZE)
    return {
      start,
      end,
      topPadding: start * ROW_HEIGHT,
      bottomPadding: Math.max(0, (all.length - end) * ROW_HEIGHT),
      slice: all.slice(start, end),
    }
  })

  const onWindowScroll = (event: Event): void => {
    const target = event.currentTarget as HTMLDivElement
    setScrollTop(target.scrollTop)
  }

  const toggleExpanded = (id: string): void => {
    setExpanded((prev) => {
      const next = new Set(prev)
      if (next.has(id)) next.delete(id)
      else next.add(id)
      return next
    })
  }

  const parseTimeInput = (value: string): number | null => {
    if (!value) return null
    const parsed = Date.parse(value)
    return Number.isNaN(parsed) ? null : parsed
  }

  const exportJson = (): void => {
    const payload = {
      sessionId: props.sessionId,
      eventCount: filtered().length,
      events: filtered().map((entry) => ({
        timestamp: entry.timestamp,
        ...entry.event,
      })),
    }
    const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const anchor = document.createElement('a')
    anchor.href = url
    anchor.download = `trajectory-${props.sessionId}.json`
    anchor.click()
    URL.revokeObjectURL(url)
  }

  const onKeydown = (event: KeyboardEvent): void => {
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'e') {
      event.preventDefault()
      exportJson()
    }
  }

  window.addEventListener('keydown', onKeydown)
  onCleanup(() => window.removeEventListener('keydown', onKeydown))

  return (
    <div class="flex h-full flex-col">
      <div class="border-b border-[var(--border-subtle)] p-3">
        <div class="mb-2 flex items-center justify-between gap-2">
          <div class="text-xs font-semibold uppercase tracking-wider text-[var(--text-muted)]">
            Trajectory
          </div>
          <button
            type="button"
            class="inline-flex items-center gap-1.5 rounded-[var(--radius-md)] border border-[var(--border-subtle)] px-2 py-1 text-[11px] text-[var(--text-secondary)] hover:bg-[var(--surface-raised)]"
            onClick={exportJson}
          >
            <Download class="h-3 w-3" />
            Export JSON
          </button>
        </div>

        <div class="grid grid-cols-2 gap-2 text-[11px]">
          <select
            value={eventTypeFilter()}
            onInput={(e) => setEventTypeFilter(e.currentTarget.value)}
            class="rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-sunken)] px-2 py-1"
          >
            <For each={eventTypes()}>{(type) => <option value={type}>{type}</option>}</For>
          </select>
          <input
            value={agentFilter()}
            onInput={(e) => setAgentFilter(e.currentTarget.value)}
            placeholder="Filter agent ID"
            class="rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-sunken)] px-2 py-1"
          />
          <input
            type="datetime-local"
            onInput={(e) => setStartMs(parseTimeInput(e.currentTarget.value))}
            class="rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-sunken)] px-2 py-1"
          />
          <input
            type="datetime-local"
            onInput={(e) => setEndMs(parseTimeInput(e.currentTarget.value))}
            class="rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-sunken)] px-2 py-1"
          />
        </div>
      </div>

      <div class="flex-1 overflow-y-auto p-2" onScroll={onWindowScroll}>
        <Show
          when={filtered().length > 0}
          fallback={<p class="p-4 text-xs text-[var(--text-muted)]">No trajectory events yet.</p>}
        >
          <div style={{ height: `${windowed().topPadding}px` }} />
          <For each={windowed().slice}>
            {(entry) => {
              const raw = entry.event as {
                args?: unknown
                output?: unknown
                durationMs?: number
                error?: string
              }
              const isExpanded = () => expanded().has(entry.id)

              return (
                <button
                  type="button"
                  onClick={() => toggleExpanded(entry.id)}
                  class={`mb-2 w-full rounded-[var(--radius-md)] border p-2 text-left ${eventTone(entry.event.type)}`}
                >
                  <div class="flex items-center justify-between gap-2 text-[11px]">
                    <div class="flex items-center gap-1.5 text-[var(--text-secondary)]">
                      <Show
                        when={entry.event.type.includes('tool:')}
                        fallback={<Brain class="h-3.5 w-3.5" />}
                      >
                        <Wrench class="h-3.5 w-3.5" />
                      </Show>
                      <Show
                        when={
                          entry.event.type.includes('doom-loop') ||
                          entry.event.type.includes('stuck')
                        }
                      >
                        <AlertTriangle class="h-3.5 w-3.5 text-[var(--warning)]" />
                      </Show>
                      <span class="font-medium text-[var(--text-primary)]">
                        {eventLabel(entry.event)}
                      </span>
                    </div>
                    <span class="inline-flex items-center gap-1 text-[var(--text-muted)]">
                      <Clock3 class="h-3 w-3" />
                      {new Date(entry.timestamp).toLocaleTimeString()}
                    </span>
                  </div>

                  <Show when={entry.event.type === 'tool:finish'}>
                    <div class="mt-1 text-[10px] text-[var(--text-muted)]">
                      Duration: {raw.durationMs ?? 0}ms
                    </div>
                  </Show>

                  <Show when={isExpanded()}>
                    <pre class="mt-2 overflow-x-auto rounded-[var(--radius-sm)] bg-[var(--surface-sunken)] p-2 text-[10px] text-[var(--text-secondary)]">
                      {JSON.stringify(entry.event, null, 2)}
                    </pre>
                    <Show when={entry.event.type === 'tool:start' && raw.args !== undefined}>
                      <div class="mt-2 text-[10px] text-[var(--text-muted)]">
                        Args available in payload
                      </div>
                    </Show>
                    <Show when={entry.event.type === 'tool:finish' && raw.output !== undefined}>
                      <div class="mt-2 text-[10px] text-[var(--text-muted)]">
                        Result available in payload
                      </div>
                    </Show>
                    <Show when={raw.error}>
                      <div class="mt-2 text-[10px] text-[var(--error)]">Error: {raw.error}</div>
                    </Show>
                  </Show>
                </button>
              )
            }}
          </For>
          <div style={{ height: `${windowed().bottomPadding}px` }} />
        </Show>
      </div>
    </div>
  )
}
