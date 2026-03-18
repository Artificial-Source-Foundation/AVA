/**
 * Tool List Dialog
 *
 * Browse all registered tools grouped by source (Built-in, MCP, Custom).
 * Each tool shows name, description, and a source badge.
 * Includes search/filter at top.
 */

import { Package, Plug, Search, Terminal, Wrench, X } from 'lucide-solid'
import { type Component, createMemo, createResource, createSignal, For, Show } from 'solid-js'
import { rustBackend } from '../../services/rust-bridge'
import type { AgentToolInfo } from '../../types/rust-ipc'

interface ToolListDialogProps {
  open: boolean
  onClose: () => void
}

type SourceCategory = 'Built-in' | 'MCP' | 'Custom' | 'Extended' | 'Other'

interface GroupedTools {
  category: SourceCategory
  tools: AgentToolInfo[]
}

function categorizeSource(source: string): SourceCategory {
  const lower = source.toLowerCase()
  if (lower.includes('builtin') || lower === 'built-in' || lower === 'builtin') return 'Built-in'
  if (lower.includes('mcp')) return 'MCP'
  if (lower.includes('custom') || lower.includes('toml')) return 'Custom'
  if (lower.includes('extended')) return 'Extended'
  return 'Other'
}

const categoryOrder: SourceCategory[] = ['Built-in', 'Extended', 'MCP', 'Custom', 'Other']

const categoryIcons: Record<SourceCategory, typeof Wrench> = {
  'Built-in': Terminal,
  Extended: Package,
  MCP: Plug,
  Custom: Wrench,
  Other: Wrench,
}

const categoryColors: Record<SourceCategory, string> = {
  'Built-in': '#A78BFA',
  Extended: '#60A5FA',
  MCP: '#34D399',
  Custom: '#FBBF24',
  Other: '#9CA3AF',
}

export const ToolListDialog: Component<ToolListDialogProps> = (props) => {
  const [query, setQuery] = createSignal('')

  const [allTools] = createResource(
    () => props.open,
    async (isOpen) => {
      if (!isOpen) return []
      try {
        return await rustBackend.listAgentTools()
      } catch {
        return []
      }
    }
  )

  const filteredGroups = createMemo((): GroupedTools[] => {
    const tools = allTools() ?? []
    const q = query().toLowerCase().trim()

    const filtered = q
      ? tools.filter(
          (t) =>
            t.name.toLowerCase().includes(q) ||
            t.description.toLowerCase().includes(q) ||
            t.source.toLowerCase().includes(q)
        )
      : tools

    const groups = new Map<SourceCategory, AgentToolInfo[]>()
    for (const tool of filtered) {
      const cat = categorizeSource(tool.source)
      const list = groups.get(cat) ?? []
      list.push(tool)
      groups.set(cat, list)
    }

    return categoryOrder
      .filter((cat) => groups.has(cat))
      .map((cat) => ({
        category: cat,
        tools: groups.get(cat)!.sort((a, b) => a.name.localeCompare(b.name)),
      }))
  })

  const totalCount = createMemo(() => (allTools() ?? []).length)

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-[#09090B]/80">
        <div class="bg-[#18181B] border border-[var(--border-default)] rounded-[var(--radius-xl)] max-w-lg w-full shadow-2xl overflow-hidden max-h-[80vh] flex flex-col">
          {/* Header */}
          <div class="flex items-center justify-between px-5 py-4 border-b border-[var(--border-subtle)]">
            <div class="flex items-center gap-2.5">
              <div class="p-1.5 rounded-[var(--radius-md)] bg-[#A78BFA15]">
                <Wrench class="w-4 h-4 text-[#A78BFA]" />
              </div>
              <div>
                <h3 class="text-sm font-semibold text-[var(--text-primary)]">Registered Tools</h3>
                <p class="text-xs text-[var(--text-muted)]">
                  {totalCount()} tool{totalCount() !== 1 ? 's' : ''} available
                </p>
              </div>
            </div>
            <button
              type="button"
              onClick={props.onClose}
              class="p-1.5 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)] transition-colors"
              aria-label="Close"
            >
              <X class="w-4 h-4" />
            </button>
          </div>

          {/* Search */}
          <div class="px-5 py-3 border-b border-[var(--border-subtle)]">
            <div class="relative">
              <Search class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-muted)]" />
              <input
                type="text"
                placeholder="Filter tools..."
                value={query()}
                onInput={(e) => setQuery(e.currentTarget.value)}
                onKeyDown={(e) => {
                  if (e.key === 'Escape') {
                    if (query()) setQuery('')
                    else props.onClose()
                  }
                }}
                class="w-full pl-9 pr-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none placeholder:text-[var(--text-muted)]"
                autofocus
              />
            </div>
          </div>

          {/* Tool list */}
          <div class="flex-1 overflow-y-auto px-5 py-3 space-y-5" style={{ 'min-height': '200px' }}>
            <Show
              when={!allTools.loading}
              fallback={
                <div class="text-center py-12">
                  <p class="text-sm text-[var(--text-muted)]">Loading tools...</p>
                </div>
              }
            >
              <Show
                when={filteredGroups().length > 0}
                fallback={
                  <div class="text-center py-12">
                    <Search class="w-8 h-8 text-[var(--text-muted)] mx-auto mb-2 opacity-40" />
                    <p class="text-sm text-[var(--text-muted)]">
                      {query() ? 'No tools match your search' : 'No tools registered'}
                    </p>
                  </div>
                }
              >
                <For each={filteredGroups()}>
                  {(group) => {
                    const Icon = categoryIcons[group.category]
                    const color = categoryColors[group.category]
                    return (
                      <div>
                        {/* Category header */}
                        <div class="flex items-center gap-2 mb-2">
                          <Icon class="w-3.5 h-3.5" style={{ color }} />
                          <span
                            class="text-xs font-semibold uppercase tracking-wider"
                            style={{ color }}
                          >
                            {group.category}
                          </span>
                          <span class="text-[11px] text-[var(--text-muted)]">
                            ({group.tools.length})
                          </span>
                        </div>

                        {/* Tools */}
                        <div class="space-y-1">
                          <For each={group.tools}>
                            {(tool) => (
                              <div class="flex items-start gap-3 px-3 py-2.5 rounded-[var(--radius-md)] hover:bg-[var(--surface-raised)] transition-colors group">
                                <div class="flex-1 min-w-0">
                                  <div class="flex items-center gap-2">
                                    <span class="text-sm font-medium text-[var(--text-primary)] font-mono">
                                      {tool.name}
                                    </span>
                                    <span
                                      class="text-[10px] font-medium px-1.5 py-0.5 rounded-full"
                                      style={{
                                        background: `${color}15`,
                                        color,
                                      }}
                                    >
                                      {group.category}
                                    </span>
                                  </div>
                                  <p class="text-xs text-[var(--text-muted)] mt-0.5 line-clamp-2">
                                    {tool.description}
                                  </p>
                                </div>
                              </div>
                            )}
                          </For>
                        </div>
                      </div>
                    )
                  }}
                </For>
              </Show>
            </Show>
          </div>

          {/* Footer */}
          <div class="px-5 py-3 border-t border-[var(--border-subtle)] flex justify-end">
            <button
              type="button"
              onClick={props.onClose}
              class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
            >
              Close
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}
