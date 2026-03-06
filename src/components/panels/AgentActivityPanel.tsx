/**
 * Agent Activity Panel Component
 *
 * Shows running agents, their status, tasks, and activity.
 * Connected to the session store for real-time agent tracking
 * and useAgent hook for live tool activity.
 */

import { AlertCircle, Bot, RefreshCw, Wrench, Zap } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useSession } from '../../stores/session'
import { AgentCard } from './activity/AgentCard'
import { ToolActivitySection } from './activity/ToolActivitySection'

interface AgentActivityPanelProps {
  compact?: boolean
}

export const AgentActivityPanel: Component<AgentActivityPanelProps> = (props) => {
  const { agents, agentStats } = useSession()
  const agent = useAgent()

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
                {agentStats().running} running &middot; {agentStats().completed} completed
              </p>
            </div>
          </div>
          <span
            class="
              p-2
              inline-flex items-center justify-center
              rounded-[var(--radius-md)]
              text-[var(--text-tertiary)]
            "
            aria-hidden="true"
          >
            <RefreshCw class="w-4 h-4" />
          </span>
        </div>
      </Show>

      {/* Live Tool Activity */}
      <ToolActivitySection />

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
          <For each={agents()}>{(ag) => <AgentCard agent={ag} />}</For>
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

/** Skeleton placeholder shown while agent data is loading */
export const AgentActivitySkeleton: Component = () => (
  <div class="flex flex-col h-full animate-pulse">
    <div class="px-4 py-3 border-b border-[var(--border-subtle)]">
      <div class="flex items-center gap-3">
        <div class="w-9 h-9 bg-[var(--surface-raised)] rounded-[var(--radius-lg)]" />
        <div class="space-y-1.5">
          <div class="h-3.5 w-24 bg-[var(--surface-raised)] rounded" />
          <div class="h-2.5 w-32 bg-[var(--surface-raised)] rounded" />
        </div>
      </div>
    </div>
    <div class="flex-1 p-3 space-y-2">
      <For each={[1, 2, 3]}>
        {() => (
          <div class="p-3 rounded-[var(--radius-lg)] border border-[var(--border-subtle)]">
            <div class="flex items-start gap-3">
              <div class="w-8 h-8 bg-[var(--surface-raised)] rounded-[var(--radius-md)]" />
              <div class="flex-1 space-y-1.5">
                <div class="h-3 w-20 bg-[var(--surface-raised)] rounded" />
                <div class="h-2.5 w-36 bg-[var(--surface-raised)] rounded" />
              </div>
            </div>
          </div>
        )}
      </For>
    </div>
  </div>
)
