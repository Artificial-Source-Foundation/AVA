/**
 * Agents Tab — Left Panel (List)
 *
 * Tier-grouped agent list with search, import/export, and toggle switches.
 * Shows agent descriptions as subtitles for context.
 */

import { type Component, createMemo, For, Show } from 'solid-js'
import type { AgentPreset, AgentTier } from '../../../config/defaults/agent-defaults'

export interface AgentsTabListProps {
  agents: AgentPreset[]
  selectedId: string | null
  onSelect: (id: string) => void
  onToggle: (id: string, enabled: boolean) => void
  searchQuery: string
  onSearchChange: (query: string) => void
  onImport?: () => void
  onExport?: () => void
  onCreate?: () => void
}

const TIER_ORDER: AgentTier[] = ['commander', 'lead', 'worker']
const TIER_LABELS: Record<AgentTier, string> = {
  commander: 'Commander',
  lead: 'Leads',
  worker: 'Workers',
}

export const AgentsTabList: Component<AgentsTabListProps> = (props) => {
  const filtered = createMemo(() => {
    const q = props.searchQuery.toLowerCase()
    if (!q) return props.agents
    return props.agents.filter(
      (a) =>
        a.name.toLowerCase().includes(q) ||
        a.description.toLowerCase().includes(q) ||
        a.tier?.toLowerCase().includes(q)
    )
  })

  const byTier = (tier: AgentTier) => filtered().filter((a) => a.tier === tier && !a.isCustom)
  const custom = () => filtered().filter((a) => a.isCustom)
  const other = () => filtered().filter((a) => !a.tier && !a.isCustom)

  return (
    <div class="flex flex-col h-full min-h-0 border-r border-[var(--border-subtle)]">
      {/* Search + Actions */}
      <div class="p-2 space-y-2 flex-shrink-0">
        <input
          type="text"
          placeholder="Search agents..."
          value={props.searchQuery}
          onInput={(e) => props.onSearchChange(e.currentTarget.value)}
          class="w-full px-2.5 py-1.5 bg-[var(--input-background)] border border-[var(--input-border)] rounded-[var(--radius-md)] text-[var(--settings-text-input)] text-[var(--text-primary)] placeholder:text-[var(--input-placeholder)] focus:outline-none focus:border-[var(--input-border-focus)]"
        />
        <div class="flex gap-1">
          <Show when={props.onImport}>
            <button
              type="button"
              onClick={() => props.onImport?.()}
              class="px-2 py-1 text-[var(--settings-text-badge)] text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] rounded-[var(--radius-sm)] transition-colors"
            >
              Import
            </button>
          </Show>
          <Show when={props.onExport}>
            <button
              type="button"
              onClick={() => props.onExport?.()}
              class="px-2 py-1 text-[var(--settings-text-badge)] text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] rounded-[var(--radius-sm)] transition-colors"
            >
              Export
            </button>
          </Show>
          <div class="flex-1" />
          <Show when={props.onCreate}>
            <button
              type="button"
              onClick={() => props.onCreate?.()}
              class="px-2 py-1 text-[var(--settings-text-badge)] font-medium text-[var(--accent)] hover:bg-[var(--accent-subtle)] rounded-[var(--radius-sm)] transition-colors"
            >
              + New
            </button>
          </Show>
        </div>
      </div>

      {/* Scrollable list */}
      <div
        class="settings-scroll-area flex-1 overflow-y-auto px-1 pb-2"
        style={{ 'overscroll-behavior': 'contain', 'scrollbar-gutter': 'stable' }}
      >
        <For each={TIER_ORDER}>
          {(tier) => (
            <Show when={byTier(tier).length > 0}>
              <TierSection
                label={TIER_LABELS[tier]}
                agents={byTier(tier)}
                selectedId={props.selectedId}
                onSelect={props.onSelect}
                onToggle={props.onToggle}
              />
            </Show>
          )}
        </For>

        <Show when={custom().length > 0}>
          <TierSection
            label="Custom"
            agents={custom()}
            selectedId={props.selectedId}
            onSelect={props.onSelect}
            onToggle={props.onToggle}
          />
        </Show>

        <Show when={other().length > 0}>
          <TierSection
            label="Other"
            agents={other()}
            selectedId={props.selectedId}
            onSelect={props.onSelect}
            onToggle={props.onToggle}
          />
        </Show>

        <Show when={filtered().length === 0}>
          <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)] text-center py-4">
            No agents found
          </p>
        </Show>
      </div>
    </div>
  )
}

// ============================================================================
// Tier Section + Agent Row
// ============================================================================

const TierSection: Component<{
  label: string
  agents: AgentPreset[]
  selectedId: string | null
  onSelect: (id: string) => void
  onToggle: (id: string, enabled: boolean) => void
}> = (props) => (
  <div class="mb-2">
    <h4 class="px-2 py-1 text-[var(--settings-text-caption)] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
      {props.label}
    </h4>
    <For each={props.agents}>
      {(agent) => (
        <button
          type="button"
          onClick={() => props.onSelect(agent.id)}
          class={`w-full flex items-start gap-2 px-2 py-1.5 rounded-[var(--radius-md)] text-left transition-colors ${
            props.selectedId === agent.id
              ? 'bg-[var(--accent-subtle)] border border-[var(--accent-muted)]'
              : 'hover:bg-[var(--alpha-white-5)] border border-transparent'
          }`}
        >
          <span
            class={`w-1.5 h-1.5 rounded-full flex-shrink-0 mt-1.5 ${agent.enabled ? 'bg-[var(--success)]' : 'bg-[var(--border-default)]'}`}
          />
          <div class="flex-1 min-w-0">
            <span class="text-[var(--settings-text-input)] text-[var(--text-secondary)] truncate block">
              {agent.name}
            </span>
            <span class="text-[var(--settings-text-caption)] text-[var(--text-muted)] truncate block leading-tight">
              {agent.description}
            </span>
          </div>
        </button>
      )}
    </For>
  </div>
)
