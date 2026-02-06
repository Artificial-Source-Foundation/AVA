/**
 * Agent Activity Panel Component
 *
 * Shows running agents, their status, tasks, and activity.
 * Connected to the session store for real-time agent tracking
 * and useAgent hook for live tool activity.
 */

import {
  AlertCircle,
  Bot,
  CheckCircle2,
  ChevronDown,
  ChevronRight,
  Clock,
  Code2,
  FileText,
  Loader2,
  Play,
  RefreshCw,
  Search,
  Terminal,
  Wrench,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { type ToolActivity, useAgent } from '../../hooks/useAgent'
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

// Tool status icons
const toolStatusIcons = {
  pending: Clock,
  running: Loader2,
  success: CheckCircle2,
  error: AlertCircle,
} as const

// Tool name to icon mapping
const toolIcons: Record<string, typeof Terminal> = {
  bash: Terminal,
  read_file: FileText,
  write_file: FileText,
  create_file: FileText,
  edit: Code2,
  glob: Search,
  grep: Search,
  ls: FileText,
  websearch: Search,
  webfetch: Search,
  browser: Play,
}

interface AgentActivityPanelProps {
  compact?: boolean
}

export const AgentActivityPanel: Component<AgentActivityPanelProps> = (props) => {
  const { agents, agentStats } = useSession()
  const agent = useAgent()
  const [selectedAgent, setSelectedAgent] = createSignal<string | null>(null)
  const [showToolActivity, setShowToolActivity] = createSignal(true)

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
      {/* Header (hidden in compact/embedded mode) */}
      <Show when={!props.compact}>
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
      </Show>

      {/* Live Tool Activity (from useAgent hook) */}
      <Show when={agent.isRunning() || agent.toolActivity().length > 0}>
        <div class="border-b border-[var(--border-subtle)]">
          <button
            type="button"
            onClick={() => setShowToolActivity(!showToolActivity())}
            class="
              w-full flex items-center justify-between
              px-4 py-2
              text-left
              hover:bg-[var(--surface-raised)]
              transition-colors
            "
          >
            <div class="flex items-center gap-2">
              <Wrench class="w-4 h-4 text-[var(--accent)]" />
              <span class="text-sm font-medium text-[var(--text-primary)]">Tool Activity</span>
              <Show when={agent.isRunning()}>
                <span class="px-1.5 py-0.5 font-[var(--font-ui-mono)] text-[10px] tracking-wide bg-[var(--accent-subtle)] text-[var(--accent)] rounded-[var(--radius-sm)]">
                  Live
                </span>
              </Show>
            </div>
            <ChevronDown
              class={`
                w-4 h-4 text-[var(--text-muted)]
                transition-transform duration-200
                ${showToolActivity() ? '' : '-rotate-90'}
              `}
            />
          </button>

          <Show when={showToolActivity()}>
            <div class="px-4 pb-3 space-y-2">
              {/* Current thought */}
              <Show when={agent.currentThought() && agent.isRunning()}>
                <div class="p-2 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] text-xs">
                  <div class="flex items-center gap-1.5 text-[var(--text-muted)] mb-1">
                    <Bot class="w-3 h-3" />
                    <span>Thinking...</span>
                  </div>
                  <p class="text-[var(--text-secondary)] line-clamp-2">
                    {agent.currentThought().slice(-200)}
                  </p>
                </div>
              </Show>

              {/* Tool calls timeline */}
              <For each={agent.toolActivity().slice(-10).reverse()}>
                {(tool: ToolActivity) => {
                  const StatusIcon = toolStatusIcons[tool.status]
                  const ToolIcon = toolIcons[tool.name] || Wrench
                  const statusColors = {
                    pending: 'var(--text-muted)',
                    running: 'var(--accent)',
                    success: 'var(--success)',
                    error: 'var(--error)',
                  }
                  const statusBgs = {
                    pending: 'var(--surface-raised)',
                    running: 'var(--accent-subtle)',
                    success: 'var(--success-subtle)',
                    error: 'var(--error-subtle)',
                  }

                  return (
                    <div
                      class={`
                        flex items-start gap-2 p-2
                        rounded-[var(--radius-md)]
                        border
                        ${
                          tool.status === 'running'
                            ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                            : 'border-[var(--border-subtle)] bg-[var(--surface)]'
                        }
                      `}
                    >
                      <div
                        class="p-1.5 rounded-[var(--radius-sm)] flex-shrink-0"
                        style={{ background: statusBgs[tool.status] }}
                      >
                        <ToolIcon
                          class="w-3.5 h-3.5"
                          style={{ color: statusColors[tool.status] }}
                        />
                      </div>
                      <div class="flex-1 min-w-0">
                        <div class="flex items-center justify-between gap-2">
                          <span class="font-[var(--font-ui-mono)] text-[12px] tracking-wide font-medium text-[var(--text-primary)]">
                            {tool.name}
                          </span>
                          <div class="flex items-center gap-1">
                            <StatusIcon
                              class={`w-3 h-3 ${tool.status === 'running' ? 'animate-spin' : ''}`}
                              style={{ color: statusColors[tool.status] }}
                            />
                            <Show when={tool.durationMs}>
                              <span class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-muted)] tabular-nums">
                                {tool.durationMs! < 1000
                                  ? `${tool.durationMs}ms`
                                  : `${(tool.durationMs! / 1000).toFixed(1)}s`}
                              </span>
                            </Show>
                          </div>
                        </div>
                        <Show when={tool.output && tool.status === 'success'}>
                          <p class="text-xs text-[var(--text-secondary)] mt-1 line-clamp-1">
                            {tool.output!.slice(0, 100)}
                          </p>
                        </Show>
                        <Show when={tool.error}>
                          <p class="text-xs text-[var(--error)] mt-1 line-clamp-1">{tool.error}</p>
                        </Show>
                      </div>
                    </div>
                  )
                }}
              </For>

              <Show when={agent.toolActivity().length === 0 && !agent.currentThought()}>
                <p class="text-xs text-[var(--text-muted)] text-center py-2">
                  No tool activity yet
                </p>
              </Show>
            </div>
          </Show>
        </div>
      </Show>

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
                        <span class="font-[var(--font-ui-mono)] text-[12px] tracking-wide font-medium text-[var(--text-primary)] truncate capitalize">
                          {agent.type}
                        </span>
                        <div class="flex items-center gap-1.5 flex-shrink-0">
                          <StatusIcon
                            class={`w-3.5 h-3.5 ${displayStatus === 'running' ? 'animate-spin' : ''}`}
                            style={{ color: config.color }}
                          />
                          <span
                            class="font-[var(--font-ui-mono)] text-[10px] tracking-wide capitalize"
                            style={{ color: config.color }}
                          >
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
                          <div class="h-1 bg-[var(--surface-sunken)] rounded-[var(--radius-sm)] overflow-hidden">
                            <div
                              class="h-full bg-[var(--accent)] rounded-[var(--radius-sm)] transition-[width] duration-300"
                              style={{ width: `${progress}%` }}
                            />
                          </div>
                          <div class="flex justify-between mt-1">
                            <span class="font-[var(--font-ui-mono)] text-[10px] tabular-nums text-[var(--text-muted)]">
                              {progress}%
                            </span>
                            <span class="font-[var(--font-ui-mono)] text-[10px] tabular-nums text-[var(--text-muted)]">
                              {formatDuration(getAgentDuration(agent))}
                            </span>
                          </div>
                        </div>
                      </Show>

                      {/* Expanded details */}
                      <Show when={selectedAgent() === agent.id}>
                        <div class="mt-3 pt-3 border-t border-[var(--border-subtle)] space-y-2">
                          {/* Model info */}
                          <div class="flex items-center gap-1.5 font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-muted)]">
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
                            <div class="flex items-center gap-1.5 font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-muted)]">
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

      {/* Summary Footer (hidden in compact/embedded mode) */}
      <Show when={!props.compact}>
        <div
          class="
            px-4 py-3
            border-t border-[var(--border-subtle)]
            bg-[var(--surface-sunken)]
          "
        >
          <div class="flex items-center justify-between font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-muted)]">
            <div class="flex items-center gap-4">
              <Show when={agent.isRunning()}>
                <span class="flex items-center gap-1.5">
                  <span class="w-2 h-2 rounded-full bg-[var(--accent)] animate-pulse" />
                  Turn {agent.currentTurn()}
                </span>
                <span class="flex items-center gap-1.5">
                  <Wrench class="w-3 h-3" />
                  {agent.toolActivity().length} tools
                </span>
              </Show>
              <Show when={!agent.isRunning()}>
                <span class="flex items-center gap-1.5">
                  <span class="w-2 h-2 rounded-full bg-[var(--accent)] animate-pulse" />
                  {agentStats().running} Active
                </span>
                <span class="flex items-center gap-1.5">
                  <span class="w-2 h-2 rounded-full bg-[var(--success)]" />
                  {agentStats().completed} Done
                </span>
              </Show>
              <Show when={agentStats().error > 0}>
                <span class="flex items-center gap-1.5">
                  <span class="w-2 h-2 rounded-full bg-[var(--error)]" />
                  {agentStats().error} Errors
                </span>
              </Show>
            </div>
            <Show
              when={agent.isRunning()}
              fallback={<span>Total: {agentStats().total} agents</span>}
            >
              <span class="flex items-center gap-1.5">
                <Zap class="w-3 h-3 text-[var(--accent)]" />
                {agent.tokensUsed().toLocaleString()} tokens
              </span>
            </Show>
          </div>

          {/* Doom loop warning */}
          <Show when={agent.doomLoopDetected()}>
            <div class="mt-2 p-2 bg-[var(--warning-subtle)] border border-[var(--warning)] rounded-[var(--radius-md)] flex items-center gap-2">
              <AlertCircle class="w-4 h-4 text-[var(--warning)]" />
              <span class="text-xs text-[var(--warning)]">Loop detected - agent may be stuck</span>
            </div>
          </Show>
        </div>
      </Show>
    </div>
  )
}
