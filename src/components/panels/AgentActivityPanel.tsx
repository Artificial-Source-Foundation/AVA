/**
 * Agent Activity Panel Component
 *
 * Shows running agents, their status, tasks, and activity.
 * Premium design with real-time activity indicators.
 */

import {
  AlertCircle,
  Bot,
  CheckCircle2,
  ChevronRight,
  Clock,
  Code2,
  FileText,
  Loader2,
  RefreshCw,
  Search,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

// Mock agent data for design preview
const mockAgents = [
  {
    id: '1',
    name: 'Code Analyzer',
    status: 'running' as const,
    task: 'Analyzing src/components for patterns...',
    icon: Code2,
    progress: 67,
    startedAt: Date.now() - 45000,
  },
  {
    id: '2',
    name: 'File Reader',
    status: 'completed' as const,
    task: 'Read 12 files from src/services',
    icon: FileText,
    progress: 100,
    startedAt: Date.now() - 120000,
    completedAt: Date.now() - 30000,
  },
  {
    id: '3',
    name: 'Search Agent',
    status: 'pending' as const,
    task: 'Waiting to search documentation...',
    icon: Search,
    progress: 0,
  },
  {
    id: '4',
    name: 'Code Generator',
    status: 'error' as const,
    task: 'Failed: Rate limit exceeded',
    icon: Code2,
    progress: 23,
    error: 'Rate limit exceeded. Retry in 30s.',
  },
]

type AgentStatus = 'pending' | 'running' | 'completed' | 'error'

const statusConfig: Record<AgentStatus, { color: string; bg: string; icon: typeof CheckCircle2 }> =
  {
    pending: { color: 'var(--text-muted)', bg: 'var(--surface-raised)', icon: Clock },
    running: { color: 'var(--accent)', bg: 'var(--accent-subtle)', icon: Loader2 },
    completed: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: CheckCircle2 },
    error: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: AlertCircle },
  }

export const AgentActivityPanel: Component = () => {
  const [selectedAgent, setSelectedAgent] = createSignal<string | null>(null)

  const formatDuration = (ms: number): string => {
    const seconds = Math.floor(ms / 1000)
    if (seconds < 60) return `${seconds}s`
    const minutes = Math.floor(seconds / 60)
    return `${minutes}m ${seconds % 60}s`
  }

  const runningCount = () => mockAgents.filter((a) => a.status === 'running').length
  const completedCount = () => mockAgents.filter((a) => a.status === 'completed').length

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div
        class="
          flex items-center justify-between
          px-4 py-3
          border-b border-[var(--border-subtle)]
        "
      >
        <div class="flex items-center gap-3">
          <div
            class="
              p-2
              bg-[var(--accent-subtle)]
              rounded-[var(--radius-lg)]
            "
          >
            <Bot class="w-5 h-5 text-[var(--accent)]" />
          </div>
          <div>
            <h2 class="text-sm font-semibold text-[var(--text-primary)]">Agent Activity</h2>
            <p class="text-xs text-[var(--text-muted)]">
              {runningCount()} running · {completedCount()} completed
            </p>
          </div>
        </div>
        <button
          type="button"
          class="
            p-2
            rounded-[var(--radius-md)]
            text-[var(--text-tertiary)]
            hover:text-[var(--text-primary)]
            hover:bg-[var(--surface-raised)]
            transition-colors duration-[var(--duration-fast)]
          "
          title="Refresh"
        >
          <RefreshCw class="w-4 h-4" />
        </button>
      </div>

      {/* Agent List */}
      <div class="flex-1 overflow-y-auto p-3 space-y-2">
        <For each={mockAgents}>
          {(agent) => {
            const config = statusConfig[agent.status]
            const StatusIcon = config.icon
            const AgentIcon = agent.icon

            return (
              <button
                type="button"
                onClick={() => setSelectedAgent(selectedAgent() === agent.id ? null : agent.id)}
                class={`
                  w-full text-left
                  p-3
                  rounded-[var(--radius-lg)]
                  border
                  transition-all duration-[var(--duration-fast)]
                  ${
                    selectedAgent() === agent.id
                      ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                      : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                  }
                `}
              >
                <div class="flex items-start gap-3">
                  {/* Agent Icon */}
                  <div
                    class="
                      p-2
                      rounded-[var(--radius-md)]
                      flex-shrink-0
                    "
                    style={{ background: config.bg }}
                  >
                    <AgentIcon class="w-4 h-4" style={{ color: config.color }} />
                  </div>

                  {/* Agent Info */}
                  <div class="flex-1 min-w-0">
                    <div class="flex items-center justify-between gap-2">
                      <span class="text-sm font-medium text-[var(--text-primary)] truncate">
                        {agent.name}
                      </span>
                      <div class="flex items-center gap-1.5 flex-shrink-0">
                        <StatusIcon
                          class={`w-3.5 h-3.5 ${agent.status === 'running' ? 'animate-spin' : ''}`}
                          style={{ color: config.color }}
                        />
                        <span class="text-xs capitalize" style={{ color: config.color }}>
                          {agent.status}
                        </span>
                      </div>
                    </div>

                    <p class="text-xs text-[var(--text-muted)] mt-1 truncate">{agent.task}</p>

                    {/* Progress bar for running agents */}
                    <Show when={agent.status === 'running'}>
                      <div class="mt-2">
                        <div class="h-1.5 bg-[var(--surface-sunken)] rounded-full overflow-hidden">
                          <div
                            class="h-full bg-[var(--accent)] rounded-full transition-all duration-300"
                            style={{ width: `${agent.progress}%` }}
                          />
                        </div>
                        <div class="flex justify-between mt-1">
                          <span class="text-xs text-[var(--text-muted)]">{agent.progress}%</span>
                          <Show when={agent.startedAt !== undefined}>
                            <span class="text-xs text-[var(--text-muted)]">
                              {formatDuration(Date.now() - agent.startedAt!)}
                            </span>
                          </Show>
                        </div>
                      </div>
                    </Show>

                    {/* Expanded details */}
                    <Show when={selectedAgent() === agent.id}>
                      <div class="mt-3 pt-3 border-t border-[var(--border-subtle)]">
                        <Show when={agent.error}>
                          <div
                            class="
                              flex items-start gap-2
                              p-2
                              bg-[var(--error-subtle)]
                              rounded-[var(--radius-md)]
                              text-xs text-[var(--error)]
                            "
                          >
                            <AlertCircle class="w-3.5 h-3.5 flex-shrink-0 mt-0.5" />
                            <span>{agent.error}</span>
                          </div>
                        </Show>
                        <Show
                          when={agent.completedAt !== undefined && agent.startedAt !== undefined}
                        >
                          <div class="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
                            <Zap class="w-3 h-3" />
                            <span>
                              Completed in {formatDuration(agent.completedAt! - agent.startedAt!)}
                            </span>
                          </div>
                        </Show>
                      </div>
                    </Show>
                  </div>

                  {/* Expand indicator */}
                  <ChevronRight
                    class={`
                      w-4 h-4 flex-shrink-0
                      text-[var(--text-muted)]
                      transition-transform duration-[var(--duration-fast)]
                      ${selectedAgent() === agent.id ? 'rotate-90' : ''}
                    `}
                  />
                </div>
              </button>
            )
          }}
        </For>
      </div>

      {/* Summary Footer */}
      <div
        class="
          px-4 py-3
          border-t border-[var(--border-subtle)]
          bg-[var(--surface-sunken)]
        "
      >
        <div class="flex items-center justify-between text-xs text-[var(--text-muted)]">
          <div class="flex items-center gap-4">
            <span class="flex items-center gap-1.5">
              <span class="w-2 h-2 rounded-full bg-[var(--accent)] animate-pulse" />
              {runningCount()} Active
            </span>
            <span class="flex items-center gap-1.5">
              <span class="w-2 h-2 rounded-full bg-[var(--success)]" />
              {completedCount()} Done
            </span>
          </div>
          <span>Total: {mockAgents.length} agents</span>
        </div>
      </div>
    </div>
  )
}
