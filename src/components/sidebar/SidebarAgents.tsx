/**
 * Sidebar Agents View
 *
 * Agent presets and active agent status.
 * Shows available agent types and their configurations.
 */

import { Bot, CheckCircle2, Loader2, Zap } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import type { Agent } from '../../types'

type DisplayStatus = 'pending' | 'running' | 'completed' | 'error'

const statusConfig: Record<DisplayStatus, { color: string; label: string }> = {
  pending: { color: 'var(--text-muted)', label: 'Idle' },
  running: { color: 'var(--warning)', label: 'Running' },
  completed: { color: 'var(--success)', label: 'Done' },
  error: { color: 'var(--error)', label: 'Error' },
}

function getDisplayStatus(agent: Agent): DisplayStatus {
  switch (agent.status) {
    case 'idle':
    case 'waiting':
      return 'pending'
    case 'thinking':
    case 'executing':
      return 'running'
    case 'completed':
      return 'completed'
    case 'error':
      return 'error'
    default:
      return 'pending'
  }
}

export const SidebarAgents: Component = () => {
  const { agents, agentStats } = useSession()

  const stats = () => agentStats()

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between px-3 h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
        <span class="text-xs font-semibold text-[var(--text-secondary)] uppercase tracking-wider">
          Agents
        </span>
        <Show when={stats().running > 0}>
          <div class="flex items-center gap-1">
            <Loader2 class="w-3 h-3 text-[var(--warning)] animate-spin" />
            <span class="text-[10px] text-[var(--warning)]">{stats().running}</span>
          </div>
        </Show>
      </div>

      {/* Agent List */}
      <div class="flex-1 overflow-y-auto px-1.5 py-1 scrollbar-none">
        <Show
          when={agents().length > 0}
          fallback={
            <div class="text-center py-8 px-4 text-[var(--text-muted)]">
              <Bot class="w-6 h-6 mx-auto mb-2 opacity-50" />
              <p class="text-xs">No agents active</p>
              <p class="text-[10px] mt-1">Agents appear when running tasks</p>
            </div>
          }
        >
          <div class="space-y-0.5">
            <For each={agents()}>
              {(agent) => {
                const status = () => getDisplayStatus(agent)
                const config = () => statusConfig[status()]

                return (
                  <div
                    class="
                      flex items-center gap-2 px-2 py-1.5
                      rounded-[var(--radius-md)]
                      text-left
                    "
                  >
                    <Bot class="w-3.5 h-3.5 flex-shrink-0" style={{ color: config().color }} />
                    <div class="flex-1 min-w-0">
                      <div class="text-xs text-[var(--text-primary)] truncate">{agent.type}</div>
                      <div class="text-[10px] truncate" style={{ color: config().color }}>
                        {config().label}
                        <Show when={agent.model}>
                          {' · '}
                          <span class="text-[var(--text-muted)]">{agent.model}</span>
                        </Show>
                      </div>
                    </div>
                    <Show when={status() === 'running'}>
                      <Loader2 class="w-3 h-3 text-[var(--warning)] animate-spin flex-shrink-0" />
                    </Show>
                    <Show when={status() === 'completed'}>
                      <CheckCircle2 class="w-3 h-3 text-[var(--success)] flex-shrink-0" />
                    </Show>
                  </div>
                )
              }}
            </For>
          </div>
        </Show>
      </div>

      {/* Footer Stats */}
      <Show when={agents().length > 0}>
        <div class="px-3 py-1.5 border-t border-[var(--border-subtle)] flex items-center gap-2">
          <Zap class="w-3 h-3 text-[var(--text-muted)]" />
          <span class="text-[10px] text-[var(--text-muted)]">
            {stats().completed} done · {stats().running} running
          </span>
        </div>
      </Show>
    </div>
  )
}
