/**
 * Agent Card
 *
 * Expandable card showing a single agent's status, progress, and details.
 */

import { AlertCircle, Bot, ChevronRight, FileText, Zap } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { Agent } from '../../../types'
import {
  agentTypeIcons,
  formatDuration,
  getAgentDuration,
  getProgress,
  mapStatus,
  statusConfig,
} from './activity-config'

export interface AgentCardProps {
  agent: Agent
}

export const AgentCard: Component<AgentCardProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  const displayStatus = () => mapStatus(props.agent.status)
  const config = () => statusConfig[displayStatus()]
  const progress = () => getProgress(props.agent)

  return (
    <button
      type="button"
      onClick={() => setExpanded(!expanded())}
      class={`
        w-full text-left
        p-3
        rounded-[var(--radius-lg)]
        border
        transition-all duration-[var(--duration-fast)]
        ${
          expanded()
            ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
            : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
        }
      `}
    >
      <div class="flex items-start gap-3">
        {/* Agent Icon */}
        <div
          class="p-2 rounded-[var(--radius-md)] flex-shrink-0"
          style={{ background: config().bg }}
        >
          {(() => {
            const AgentIcon = agentTypeIcons[props.agent.type] || FileText
            return <AgentIcon class="w-4 h-4" style={{ color: config().color }} />
          })()}
        </div>

        {/* Agent Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center justify-between gap-2">
            <span class="font-[var(--font-ui-mono)] text-[12px] tracking-wide font-medium text-[var(--text-primary)] truncate capitalize">
              {props.agent.type}
            </span>
            <div class="flex items-center gap-1.5 flex-shrink-0">
              {(() => {
                const StatusIcon = config().icon
                return (
                  <StatusIcon
                    class={`w-3.5 h-3.5 ${displayStatus() === 'running' ? 'animate-spin' : ''}`}
                    style={{ color: config().color }}
                  />
                )
              })()}
              <span
                class="font-[var(--font-ui-mono)] text-[10px] tracking-wide capitalize"
                style={{ color: config().color }}
              >
                {displayStatus()}
              </span>
            </div>
          </div>

          <p class="text-xs text-[var(--text-muted)] mt-1 truncate">
            {props.agent.taskDescription || `Using ${props.agent.model}`}
          </p>

          {/* Progress bar for running agents */}
          <Show when={displayStatus() === 'running'}>
            <div class="mt-2">
              <div class="h-1 bg-[var(--surface-sunken)] rounded-[var(--radius-sm)] overflow-hidden">
                <div
                  class="h-full bg-[var(--accent)] rounded-[var(--radius-sm)] transition-[width] duration-300"
                  style={{ width: `${progress()}%` }}
                />
              </div>
              <div class="flex justify-between mt-1">
                <span class="font-[var(--font-ui-mono)] text-[10px] tabular-nums text-[var(--text-muted)]">
                  {progress()}%
                </span>
                <span class="font-[var(--font-ui-mono)] text-[10px] tabular-nums text-[var(--text-muted)]">
                  {formatDuration(getAgentDuration(props.agent))}
                </span>
              </div>
            </div>
          </Show>

          {/* Expanded details */}
          <Show when={expanded()}>
            <div class="mt-3 pt-3 border-t border-[var(--border-subtle)] space-y-2">
              {/* Model info */}
              <div class="flex items-center gap-1.5 font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-muted)]">
                <Bot class="w-3 h-3" />
                <span>Model: {props.agent.model}</span>
              </div>

              {/* Assigned files */}
              <Show when={props.agent.assignedFiles && props.agent.assignedFiles.length > 0}>
                <div class="text-xs text-[var(--text-muted)]">
                  <span class="flex items-center gap-1.5 mb-1">
                    <FileText class="w-3 h-3" />
                    <span>Files:</span>
                  </span>
                  <ul class="ml-4 space-y-0.5">
                    <For each={props.agent.assignedFiles!.slice(0, 3)}>
                      {(file) => <li class="truncate text-[var(--text-secondary)]">{file}</li>}
                    </For>
                    <Show when={(props.agent.assignedFiles?.length || 0) > 3}>
                      <li class="text-[var(--text-muted)]">
                        +{props.agent.assignedFiles!.length - 3} more
                      </li>
                    </Show>
                  </ul>
                </div>
              </Show>

              {/* Error message */}
              <Show when={props.agent.status === 'error' && props.agent.result?.errors?.length}>
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
                  <span>{props.agent.result!.errors![0]}</span>
                </div>
              </Show>

              {/* Completion info */}
              <Show when={props.agent.completedAt}>
                <div class="flex items-center gap-1.5 font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-muted)]">
                  <Zap class="w-3 h-3" />
                  <span>Completed in {formatDuration(getAgentDuration(props.agent))}</span>
                </div>
              </Show>

              {/* Result summary */}
              <Show when={props.agent.result?.summary}>
                <div class="text-xs text-[var(--text-secondary)] p-2 bg-[var(--surface-sunken)] rounded-[var(--radius-md)]">
                  {props.agent.result!.summary}
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
            ${expanded() ? 'rotate-90' : ''}
          `}
        />
      </div>
    </button>
  )
}
