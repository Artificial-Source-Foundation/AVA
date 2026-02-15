/**
 * Agents Settings Tab
 *
 * Flat, minimal design matching GeneralSection.
 * Configure AI agent behavior and presets.
 */

import { type Component, createSignal, For, Show } from 'solid-js'
import type { AgentPreset } from '../../../config/defaults/agent-defaults'

// Re-export so existing barrel consumers stay happy
export type { AgentPreset } from '../../../config/defaults/agent-defaults'
export { defaultAgentPresets } from '../../../config/defaults/agent-defaults'

// ============================================================================
// Types
// ============================================================================

export interface AgentsTabProps {
  agents: AgentPreset[]
  onToggle?: (id: string, enabled: boolean) => void
  onEdit?: (id: string) => void
  onDelete?: (id: string) => void
  onCreate?: () => void
}

// ============================================================================
// Agents Tab Component
// ============================================================================

export const AgentsTab: Component<AgentsTabProps> = (props) => {
  const [searchQuery, setSearchQuery] = createSignal('')

  const enabledCount = () => props.agents.filter((a) => a.enabled).length

  const filteredAgents = () => {
    const query = searchQuery().toLowerCase()
    if (!query) return props.agents
    return props.agents.filter(
      (a) =>
        a.name.toLowerCase().includes(query) ||
        a.description.toLowerCase().includes(query) ||
        a.capabilities.some((c) => c.toLowerCase().includes(query))
    )
  }

  const customAgents = () => filteredAgents().filter((a) => a.isCustom)
  const builtinAgents = () => filteredAgents().filter((a) => !a.isCustom)

  return (
    <div class="space-y-4">
      {/* Search + Add */}
      <div class="flex items-center gap-2">
        <input
          type="text"
          placeholder="Search agents..."
          value={searchQuery()}
          onInput={(e) => setSearchQuery(e.currentTarget.value)}
          class="
            flex-1 px-3 py-2
            bg-[var(--input-background)]
            border border-[var(--input-border)]
            rounded-[var(--radius-md)]
            text-xs text-[var(--text-primary)]
            placeholder:text-[var(--input-placeholder)]
            focus:outline-none focus:border-[var(--input-border-focus)]
            transition-colors
          "
        />
        <Show when={props.onCreate}>
          <button
            type="button"
            onClick={() => props.onCreate?.()}
            class="px-2.5 py-2 text-[11px] font-medium text-[var(--accent)] hover:bg-[var(--accent-subtle)] rounded-[var(--radius-md)] transition-colors"
          >
            + New
          </button>
        </Show>
      </div>

      <p class="text-[10px] text-[var(--text-muted)]">
        {enabledCount()} of {props.agents.length} enabled
      </p>

      {/* Custom Agents */}
      <Show when={customAgents().length > 0}>
        <div>
          <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
            Custom
          </h3>
          <div class="space-y-0.5">
            <For each={customAgents()}>
              {(agent) => (
                <AgentRow
                  agent={agent}
                  onToggle={(enabled) => props.onToggle?.(agent.id, enabled)}
                  onEdit={() => props.onEdit?.(agent.id)}
                  onDelete={() => props.onDelete?.(agent.id)}
                />
              )}
            </For>
          </div>
        </div>
      </Show>

      {/* Built-in Agents */}
      <Show when={builtinAgents().length > 0}>
        <div>
          <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
            Built-in
          </h3>
          <div class="space-y-0.5">
            <For each={builtinAgents()}>
              {(agent) => (
                <AgentRow
                  agent={agent}
                  onToggle={(enabled) => props.onToggle?.(agent.id, enabled)}
                  onEdit={() => props.onEdit?.(agent.id)}
                />
              )}
            </For>
          </div>
        </div>
      </Show>

      <Show when={filteredAgents().length === 0}>
        <p class="text-xs text-[var(--text-muted)] text-center py-6">No agents found</p>
      </Show>
    </div>
  )
}

// ============================================================================
// Agent Row Component
// ============================================================================

interface AgentRowProps {
  agent: AgentPreset
  onToggle?: (enabled: boolean) => void
  onEdit?: () => void
  onDelete?: () => void
}

const AgentRow: Component<AgentRowProps> = (props) => (
  <div class="flex items-center justify-between py-1.5 group">
    <div class="flex-1 min-w-0">
      <div class="flex items-center gap-1.5">
        <span class="text-xs text-[var(--text-secondary)]">{props.agent.name}</span>
        <span
          class={`w-1.5 h-1.5 rounded-full ${props.agent.enabled ? 'bg-[var(--success)]' : 'bg-[var(--border-default)]'}`}
        />
        <Show when={props.agent.isCustom}>
          <span class="text-[9px] text-[var(--accent)]">custom</span>
        </Show>
      </div>
      <p class="text-[10px] text-[var(--text-muted)] truncate">{props.agent.description}</p>
    </div>
    <div class="flex items-center gap-1.5">
      <Show when={props.onEdit}>
        <button
          type="button"
          onClick={() => props.onEdit?.()}
          class="text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] opacity-0 group-hover:opacity-100 transition-all"
        >
          Edit
        </button>
      </Show>
      <Show when={props.agent.isCustom && props.onDelete}>
        <button
          type="button"
          onClick={() => props.onDelete?.()}
          class="text-[10px] text-[var(--text-muted)] hover:text-[var(--error)] opacity-0 group-hover:opacity-100 transition-all"
        >
          Delete
        </button>
      </Show>
      <button
        type="button"
        onClick={() => props.onToggle?.(!props.agent.enabled)}
        class={`
          w-9 h-5 rounded-full transition-colors flex-shrink-0 flex items-center
          ${props.agent.enabled ? 'bg-[var(--accent)]' : 'bg-[var(--border-default)]'}
        `}
      >
        <span
          class={`
            w-4 h-4 rounded-full bg-white shadow-sm transition-transform duration-150
            ${props.agent.enabled ? 'translate-x-[18px]' : 'translate-x-[2px]'}
          `}
        />
      </button>
    </div>
  </div>
)
