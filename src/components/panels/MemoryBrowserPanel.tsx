/**
 * Memory Browser Panel
 *
 * Browse memory items across all sessions with search, filter, and delete.
 */

import {
  Bookmark,
  Brain,
  Code2,
  FileText,
  MessageSquare,
  Search,
  Sparkles,
  Trash2,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, onMount, Show } from 'solid-js'
import { deleteMemoryItem } from '../../services/database'
import { logError } from '../../services/logger'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import type { MemoryItem, MemoryItemType } from '../../types'

const TYPE_CONFIG: Record<
  MemoryItemType,
  { color: string; bg: string; icon: typeof MessageSquare; label: string }
> = {
  conversation: {
    color: 'var(--accent)',
    bg: 'var(--accent-subtle)',
    icon: MessageSquare,
    label: 'Conversation',
  },
  file: { color: 'var(--info)', bg: 'var(--info-subtle)', icon: FileText, label: 'File' },
  code: { color: 'var(--warning)', bg: 'var(--warning-subtle)', icon: Code2, label: 'Code' },
  knowledge: {
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
    icon: Sparkles,
    label: 'Knowledge',
  },
  checkpoint: {
    color: 'var(--text-muted)',
    bg: 'var(--surface-raised)',
    icon: Bookmark,
    label: 'Checkpoint',
  },
}

export const MemoryBrowserPanel: Component = () => {
  const { queryMemoriesAcrossSessions } = useSession()
  const { currentProject } = useProject()
  const [items, setItems] = createSignal<MemoryItem[]>([])
  const [search, setSearch] = createSignal('')
  const [typeFilter, setTypeFilter] = createSignal<MemoryItemType | 'all'>('all')
  const [loading, setLoading] = createSignal(false)

  const loadItems = async () => {
    setLoading(true)
    try {
      const result = await queryMemoriesAcrossSessions(currentProject()?.id)
      setItems(result)
    } finally {
      setLoading(false)
    }
  }

  onMount(() => void loadItems())

  const filtered = createMemo(() => {
    let list = items()
    const q = search().toLowerCase()
    if (q) {
      list = list.filter(
        (m) =>
          m.title.toLowerCase().includes(q) ||
          m.preview.toLowerCase().includes(q) ||
          (m.source?.toLowerCase().includes(q) ?? false)
      )
    }
    const tf = typeFilter()
    if (tf !== 'all') {
      list = list.filter((m) => m.type === tf)
    }
    return list
  })

  const handleDelete = async (id: string) => {
    try {
      await deleteMemoryItem(id)
      setItems((prev) => prev.filter((m) => m.id !== id))
    } catch (err) {
      logError('MemoryBrowser', 'Failed to delete memory item', err)
    }
  }

  const formatDate = (ts: number) => {
    const d = new Date(ts)
    return d.toLocaleDateString('en-US', {
      month: 'short',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
    })
  }

  const formatTokens = (t: number) => (t >= 1000 ? `${(t / 1000).toFixed(1)}K` : String(t))

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between density-section-px density-section-py border-b border-[var(--border-subtle)]">
        <div class="flex items-center gap-3">
          <div class="p-2 bg-[var(--accent-subtle)] rounded-[var(--radius-lg)]">
            <Brain class="w-5 h-5 text-[var(--accent)]" />
          </div>
          <div>
            <h2 class="text-sm font-semibold text-[var(--text-primary)]">Memory Browser</h2>
            <p class="text-xs text-[var(--text-muted)]">
              {filtered().length} items across all sessions
            </p>
          </div>
        </div>
      </div>

      {/* Search + Filter */}
      <div class="density-section-px density-section-py border-b border-[var(--border-subtle)] space-y-2">
        <div class="relative">
          <Search class="absolute left-2 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search memories..."
            value={search()}
            onInput={(e) => setSearch(e.currentTarget.value)}
            class="w-full pl-7 pr-2 py-1.5 text-xs text-[var(--text-primary)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] placeholder:text-[var(--text-muted)] focus-glow"
          />
        </div>
        <div class="flex gap-1 flex-wrap">
          <For each={['all' as const, ...(Object.keys(TYPE_CONFIG) as MemoryItemType[])]}>
            {(t) => (
              <button
                type="button"
                onClick={() => setTypeFilter(t)}
                class={`px-2 py-0.5 text-[10px] rounded-full transition-colors ${
                  typeFilter() === t
                    ? 'bg-[var(--accent)] text-white'
                    : 'bg-[var(--surface-raised)] text-[var(--text-muted)] hover:text-[var(--text-primary)]'
                }`}
              >
                {t === 'all' ? 'All' : TYPE_CONFIG[t].label}
              </button>
            )}
          </For>
        </div>
      </div>

      {/* Items */}
      <div class="flex-1 overflow-y-auto density-section-px py-2 space-y-1.5 scrollbar-none">
        <Show when={loading()}>
          <p class="text-xs text-[var(--text-muted)] text-center py-8">Loading...</p>
        </Show>
        <Show when={!loading() && filtered().length === 0}>
          <div class="text-center py-8 text-[var(--text-muted)]">
            <Brain class="w-6 h-6 mx-auto mb-2 opacity-50" />
            <p class="text-xs">No memory items found</p>
          </div>
        </Show>
        <For each={filtered()}>
          {(item) => {
            const cfg = TYPE_CONFIG[item.type]
            const Icon = cfg.icon
            return (
              <div class="flex items-start gap-2.5 p-2.5 rounded-[var(--radius-lg)] border border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)] transition-colors">
                <div
                  class="p-1.5 rounded-[var(--radius-md)] flex-shrink-0"
                  style={{ background: cfg.bg }}
                >
                  <Icon class="w-3.5 h-3.5" style={{ color: cfg.color }} />
                </div>
                <div class="flex-1 min-w-0">
                  <div class="flex items-center gap-2">
                    <span class="text-xs font-medium text-[var(--text-primary)] truncate">
                      {item.title}
                    </span>
                    <span
                      class="text-[9px] px-1.5 py-0.5 rounded-full flex-shrink-0"
                      style={{ background: cfg.bg, color: cfg.color }}
                    >
                      {cfg.label}
                    </span>
                  </div>
                  <p class="text-[11px] text-[var(--text-muted)] mt-0.5 line-clamp-2">
                    {item.preview}
                  </p>
                  <div class="flex items-center gap-2 mt-1 text-[10px] text-[var(--text-muted)]">
                    <span>{formatTokens(item.tokens)} tokens</span>
                    <span>·</span>
                    <span>{formatDate(item.createdAt)}</span>
                    <Show when={item.source}>
                      <span>·</span>
                      <span class="truncate">{item.source}</span>
                    </Show>
                  </div>
                </div>
                <button
                  type="button"
                  onClick={() => void handleDelete(item.id)}
                  class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--error-subtle)] transition-colors flex-shrink-0"
                  title="Delete"
                >
                  <Trash2 class="w-3 h-3" />
                </button>
              </div>
            )
          }}
        </For>
      </div>
    </div>
  )
}
