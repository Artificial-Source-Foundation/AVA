/**
 * Agents Tab — Left Panel (List)
 *
 * Flat agent list matching the Pencil design: simple rows with name,
 * selected state highlighted with accent tint. Header with title + "New" button.
 */

import { type Component, createMemo, For, Show } from 'solid-js'
import type { AgentPreset } from '../../../config/defaults/agent-defaults'

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

  return (
    <div
      class="flex flex-col h-full min-h-0"
      style={{ background: '#0A0A0C', 'border-right': '1px solid #ffffff06' }}
    >
      {/* Header with title + New button */}
      <div
        class="flex items-center justify-between flex-shrink-0"
        style={{ padding: '16px 16px 12px 16px' }}
      >
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '15px',
            'font-weight': '600',
            color: '#F5F5F7',
          }}
        >
          Agents
        </span>
        <Show when={props.onCreate}>
          <button
            type="button"
            onClick={() => props.onCreate?.()}
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '11px',
              'font-weight': '500',
              color: '#FFFFFF',
              background: '#0A84FF',
              'border-radius': '6px',
              padding: '4px 10px',
              border: 'none',
              cursor: 'pointer',
            }}
          >
            + New
          </button>
        </Show>
      </div>

      {/* Agent list */}
      <div
        class="settings-scroll-area flex-1 overflow-y-auto"
        style={{ 'overscroll-behavior': 'contain', 'scrollbar-gutter': 'stable' }}
      >
        <For each={filtered()}>
          {(agent) => {
            const isSelected = () => props.selectedId === agent.id
            return (
              <button
                type="button"
                onClick={() => props.onSelect(agent.id)}
                class="w-full text-left"
                style={{
                  display: 'flex',
                  'align-items': 'center',
                  padding: '10px 16px',
                  background: isSelected() ? '#0A84FF14' : 'transparent',
                  border: 'none',
                  'border-bottom': '1px solid #ffffff06',
                  cursor: 'pointer',
                  transition: 'background 150ms',
                }}
                onMouseEnter={(e) => {
                  if (!isSelected()) e.currentTarget.style.background = '#ffffff06'
                }}
                onMouseLeave={(e) => {
                  if (!isSelected()) e.currentTarget.style.background = 'transparent'
                }}
              >
                <span
                  style={{
                    'font-family': 'Geist, sans-serif',
                    'font-size': '13px',
                    'font-weight': isSelected() ? '500' : 'normal',
                    color: isSelected() ? '#F5F5F7' : '#C8C8CC',
                  }}
                >
                  {agent.name}
                </span>
              </button>
            )
          }}
        </For>

        <Show when={filtered().length === 0}>
          <p
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '12px',
              color: '#48484A',
              'text-align': 'center',
              padding: '16px',
            }}
          >
            No agents found
          </p>
        </Show>
      </div>
    </div>
  )
}
