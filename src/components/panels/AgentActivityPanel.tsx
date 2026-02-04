/**
 * Agent Activity Panel Component
 *
 * Shows running agents, their status, tasks, and activity.
 * Connected to the session store for real-time agent tracking.
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
import { useSession } from '../../stores/session'
import type { Agent } from '../../types'

// ============================================================================
// Status Configuration
// ============================================================================

type DisplayStatus = 'pending' | 'running' | 'completed' | 'error'

const statusConfig: Record<
  DisplayStatus,
  { color: string; bg: string; icon: typeof CheckCircle2 }
> = {
  pending: { color: 'var(--text-muted)', bg: 'var(--surface-raised)', icon: Clock },
  running: { color: 'var(--accent)', bg: 'var(--accent-subtle)', icon: Loader2 },
  completed: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: CheckCircle2 },
  error: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: AlertCircle },
}

// Map agent type to icon
const agentTypeIcons = {
  commander: Bot,
  operator: Code2,
  validator: Search,
} as const

// Map internal agent status to display status
const mapStatus = (status: Agent['status']): DisplayStatus => {
  switch (status) {
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

// ============================================================================
// Component
// ============================================================================

export const AgentActivityPanel: Component = () => {
  const { agents, agentStats } = useSession()
  const [selectedAgent, setSelectedAgent] = createSignal<string | null>(null)

  const formatDuration = (ms: number): string => {
    const seconds = Math.floor(ms / 1000)
    if (seconds < 60) return `${seconds}s`
    const minutes = Math.floor(seconds / 60)
    return `${minutes}m ${seconds % 60}s`
  }

  const getAgentDuration = (agent: Agent): number => {
    if (agent.completedAt) {
      return agent.completedAt - agent.createdAt
    }
    return Date.now() - agent.createdAt
  }

  // Calculate progress based on status (estimate)
  const getProgress = (agent: Agent): number => {
    switch (agent.status) {
      case 'idle':
        return 0
      case 'waiting':
        return 10
      case 'thinking':
        return 40
      case 'executing':
        return 70
      case 'completed':
        return 100
      case 'error':
        return agent.result ? 50 : 20
      default:
        return 0
    }
  }

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
              {agentStats().running} running · {agentStats().completed} completed
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
        <Show
          when={agents().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <div class="p-4 bg-[var(--surface-raised)] rounded-full mb-4">
                <Bot class="w-8 h-8 text-[var(--text-muted)]" />
              </div>
              <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-1">No agents yet</h3>
              <p class="text-xs text-[var(--text-muted)]">
                Agents will appear here when they start working on your tasks
              </p>
            </div>
          }
        >
          <For each={agents()}>
            {(agent) => {
              const displayStatus = mapStatus(agent.status)
              const config = statusConfig[displayStatus]
              const StatusIcon = config.icon
              const AgentIcon = agentTypeIcons[agent.type] || FileText
              const progress = getProgress(agent)

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
                        <span class="text-sm font-medium text-[var(--text-primary)] truncate capitalize">
                          {agent.type}
                        </span>
                        <div class="flex items-center gap-1.5 flex-shrink-0">
                          <StatusIcon
                            class={`w-3.5 h-3.5 ${displayStatus === 'running' ? 'animate-spin' : ''}`}
                            style={{ color: config.color }}
                          />
                          <span class="text-xs capitalize" style={{ color: config.color }}>
                            {displayStatus}
                          </span>
                        </div>
                      </div>

                      <p class="text-xs text-[var(--text-muted)] mt-1 truncate">
                        {agent.taskDescription || `Using ${agent.model}`}
                      </p>

                      {/* Progress bar for running agents */}
                      <Show when={displayStatus === 'running'}>
                        <div class="mt-2">
                          <div class="h-1.5 bg-[var(--surface-sunken)] rounded-full overflow-hidden">
                            <div
                              class="h-full bg-[var(--accent)] rounded-full transition-all duration-300"
                              style={{ width: `${progress}%` }}
                            />
                          </div>
                          <div class="flex justify-between mt-1">
                            <span class="text-xs text-[var(--text-muted)]">{progress}%</span>
                            <span class="text-xs text-[var(--text-muted)]">
                              {formatDuration(getAgentDuration(agent))}
                            </span>
                          </div>
                        </div>
                      </Show>

                      {/* Expanded details */}
                      <Show when={selectedAgent() === agent.id}>
                        <div class="mt-3 pt-3 border-t border-[var(--border-subtle)] space-y-2">
                          {/* Model info */}
                          <div class="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
                            <Bot class="w-3 h-3" />
                            <span>Model: {agent.model}</span>
                          </div>

                          {/* Assigned files */}
                          <Show when={agent.assignedFiles && agent.assignedFiles.length > 0}>
                            <div class="text-xs text-[var(--text-muted)]">
                              <span class="flex items-center gap-1.5 mb-1">
                                <FileText class="w-3 h-3" />
                                <span>Files:</span>
                              </span>
                              <ul class="ml-4 space-y-0.5">
                                <For each={agent.assignedFiles!.slice(0, 3)}>
                                  {(file) => (
                                    <li class="truncate text-[var(--text-secondary)]">{file}</li>
                                  )}
                                </For>
                                <Show when={(agent.assignedFiles?.length || 0) > 3}>
                                  <li class="text-[var(--text-muted)]">
                                    +{agent.assignedFiles!.length - 3} more
                                  </li>
                                </Show>
                              </ul>
                            </div>
                          </Show>

                          {/* Error message */}
                          <Show when={agent.status === 'error' && agent.result?.errors?.length}>
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
                              <span>{agent.result!.errors![0]}</span>
                            </div>
                          </Show>

                          {/* Completion info */}
                          <Show when={agent.completedAt}>
                            <div class="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
                              <Zap class="w-3 h-3" />
                              <span>Completed in {formatDuration(getAgentDuration(agent))}</span>
                            </div>
                          </Show>

                          {/* Result summary */}
                          <Show when={agent.result?.summary}>
                            <div class="text-xs text-[var(--text-secondary)] p-2 bg-[var(--surface-sunken)] rounded-[var(--radius-md)]">
                              {agent.result!.summary}
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
        </Show>
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
              {agentStats().running} Active
            </span>
            <span class="flex items-center gap-1.5">
              <span class="w-2 h-2 rounded-full bg-[var(--success)]" />
              {agentStats().completed} Done
            </span>
            <Show when={agentStats().error > 0}>
              <span class="flex items-center gap-1.5">
                <span class="w-2 h-2 rounded-full bg-[var(--error)]" />
                {agentStats().error} Errors
              </span>
            </Show>
          </div>
          <span>Total: {agentStats().total} agents</span>
        </div>
      </div>
    </div>
  )
}
