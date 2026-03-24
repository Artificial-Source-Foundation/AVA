/**
 * Message Queue Widget
 *
 * Renders above the composer textarea when there are queued messages.
 * Supports viewing, inline editing, reordering, and removing queued
 * items. Post-complete messages are shown in a separate section with
 * group numbers.
 */

import { ArrowDown, ArrowUp, Check, ListOrdered, Pencil, X } from 'lucide-solid'
import { type Accessor, type Component, createMemo, createSignal, For, Show } from 'solid-js'

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface QueuedItem {
  id: string
  content: string
  tier: 'queued' | 'interrupt' | 'post-complete' | 'follow-up' | 'steering'
  group?: number
}

export interface MessageQueueWidgetProps {
  queuedMessages: Accessor<QueuedItem[]>
  onRemove: (index: number) => void
  onReorder: (fromIndex: number, toIndex: number) => void
  onEdit: (index: number, newContent: string) => void
  onClearAll: () => void
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function tierLabel(tier: QueuedItem['tier']): string {
  switch (tier) {
    case 'queued':
    case 'follow-up':
      return 'Q'
    case 'interrupt':
    case 'steering':
      return 'I'
    case 'post-complete':
      return 'G'
    default:
      return ''
  }
}

function tierColor(tier: QueuedItem['tier']): string {
  switch (tier) {
    case 'interrupt':
    case 'steering':
      return 'var(--warning)'
    case 'queued':
    case 'follow-up':
      return 'var(--accent)'
    case 'post-complete':
      return 'var(--text-muted)'
    default:
      return 'var(--text-muted)'
  }
}

function truncate(text: string, max: number): string {
  const single = text.replace(/\n/g, ' ').trim()
  if (single.length <= max) return single
  return `${single.slice(0, max)}...`
}

// ---------------------------------------------------------------------------
// Inline Edit Sub-component
// ---------------------------------------------------------------------------

interface InlineEditProps {
  value: string
  onSave: (newValue: string) => void
  onCancel: () => void
}

const InlineEdit: Component<InlineEditProps> = (props) => {
  let ref: HTMLTextAreaElement | undefined
  const [draft, setDraft] = createSignal(props.value)

  const handleKeyDown = (e: KeyboardEvent): void => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      const trimmed = draft().trim()
      if (trimmed) props.onSave(trimmed)
    }
    if (e.key === 'Escape') {
      e.preventDefault()
      props.onCancel()
    }
  }

  // Auto-focus on mount
  queueMicrotask(() => ref?.focus())

  return (
    <div class="flex items-start gap-1 flex-1 min-w-0">
      <textarea
        ref={ref}
        value={draft()}
        onInput={(e) => setDraft(e.currentTarget.value)}
        onKeyDown={handleKeyDown}
        rows={1}
        class="flex-1 min-w-0 px-1.5 py-0.5 text-xs bg-[var(--gray-2)] text-[var(--text-primary)] border border-[var(--accent)] rounded resize-none outline-none"
        style={{ 'min-height': '24px', 'max-height': '80px' }}
      />
      <button
        type="button"
        onClick={() => {
          const trimmed = draft().trim()
          if (trimmed) props.onSave(trimmed)
        }}
        class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--accent)] cursor-pointer flex-shrink-0"
        title="Save"
      >
        <Check class="w-3 h-3" />
      </button>
      <button
        type="button"
        onClick={props.onCancel}
        class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] cursor-pointer flex-shrink-0"
        title="Cancel"
      >
        <X class="w-3 h-3" />
      </button>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Queue Row Sub-component
// ---------------------------------------------------------------------------

interface QueueRowProps {
  item: QueuedItem
  index: number
  total: number
  isEditing: boolean
  onStartEdit: () => void
  onSaveEdit: (newContent: string) => void
  onCancelEdit: () => void
  onRemove: () => void
  onMoveUp: () => void
  onMoveDown: () => void
}

const QueueRow: Component<QueueRowProps> = (props) => (
  <div class="flex items-center gap-1.5 py-1 px-2 rounded bg-[var(--alpha-white-5)] group">
    {/* Position / tier badge */}
    <span
      class="text-[var(--text-2xs)] font-mono font-bold flex-shrink-0 w-5 text-center"
      style={{ color: tierColor(props.item.tier) }}
    >
      {props.item.tier === 'post-complete'
        ? `G${props.item.group ?? 1}`
        : `${tierLabel(props.item.tier)}${props.index + 1}`}
    </span>

    {/* Content or inline edit */}
    <Show
      when={!props.isEditing}
      fallback={
        <InlineEdit
          value={props.item.content}
          onSave={props.onSaveEdit}
          onCancel={props.onCancelEdit}
        />
      }
    >
      <span class="text-xs text-[var(--text-secondary)] flex-1 min-w-0 truncate">
        {truncate(props.item.content, 80)}
      </span>
    </Show>

    {/* Action buttons — visible on hover */}
    <Show when={!props.isEditing}>
      <div class="flex items-center gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity flex-shrink-0">
        <button
          type="button"
          onClick={props.onStartEdit}
          class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--accent)] cursor-pointer transition-colors"
          title="Edit"
        >
          <Pencil class="w-3 h-3" />
        </button>
        <Show when={props.index > 0}>
          <button
            type="button"
            onClick={props.onMoveUp}
            class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--text-primary)] cursor-pointer transition-colors"
            title="Move up"
          >
            <ArrowUp class="w-3 h-3" />
          </button>
        </Show>
        <Show when={props.index < props.total - 1}>
          <button
            type="button"
            onClick={props.onMoveDown}
            class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--text-primary)] cursor-pointer transition-colors"
            title="Move down"
          >
            <ArrowDown class="w-3 h-3" />
          </button>
        </Show>
        <button
          type="button"
          onClick={props.onRemove}
          class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--error)] cursor-pointer transition-colors"
          title="Remove"
        >
          <X class="w-3 h-3" />
        </button>
      </div>
    </Show>
  </div>
)

// ---------------------------------------------------------------------------
// Main Widget
// ---------------------------------------------------------------------------

export const MessageQueueWidget: Component<MessageQueueWidgetProps> = (props) => {
  const [expanded, setExpanded] = createSignal(true)
  const [editingIndex, setEditingIndex] = createSignal<number | null>(null)

  const count = createMemo(() => props.queuedMessages().length)

  // Split into regular (queued + interrupt) and post-complete
  const regularItems = createMemo(() =>
    props.queuedMessages().filter((m) => m.tier !== 'post-complete')
  )
  const postCompleteItems = createMemo(() =>
    props.queuedMessages().filter((m) => m.tier === 'post-complete')
  )

  // Get absolute index in the full list for a given item
  const absoluteIndex = (item: QueuedItem): number =>
    props.queuedMessages().findIndex((m) => m.id === item.id)

  return (
    <Show when={count() > 0}>
      <div class="mb-1.5 rounded-lg border border-[var(--border-subtle)] bg-[var(--surface-raised)] overflow-hidden animate-[approvalSlideUp_150ms_ease-out]">
        {/* Compact header */}
        <div class="flex items-center gap-2 px-3 py-1.5">
          <button
            type="button"
            onClick={() => setExpanded((e) => !e)}
            class="flex items-center gap-2 flex-1 min-w-0 text-left cursor-pointer hover:opacity-80 transition-opacity"
          >
            <ListOrdered class="w-3.5 h-3.5 text-[var(--accent)] flex-shrink-0" />
            <span class="text-[var(--text-xs)] font-medium text-[var(--text-secondary)]">
              {count()} queued message{count() > 1 ? 's' : ''}
            </span>
            <span class="text-[var(--text-2xs)] text-[var(--text-muted)]">
              {expanded() ? '\u25B4' : '\u25BE'}
            </span>
          </button>

          <button
            type="button"
            onClick={props.onClearAll}
            class="text-[var(--text-2xs)] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors cursor-pointer px-1.5 py-0.5 rounded hover:bg-[var(--alpha-white-5)]"
          >
            Clear all
          </button>
        </div>

        {/* Expanded message list */}
        <Show when={expanded()}>
          <div class="px-3 pb-2 space-y-1 max-h-44 overflow-y-auto scrollbar-thin">
            {/* Regular queued messages (queued + interrupt) */}
            <Show when={regularItems().length > 0}>
              <For each={regularItems()}>
                {(item, localIdx) => {
                  const absIdx = () => absoluteIndex(item)
                  return (
                    <QueueRow
                      item={item}
                      index={localIdx()}
                      total={regularItems().length}
                      isEditing={editingIndex() === absIdx()}
                      onStartEdit={() => setEditingIndex(absIdx())}
                      onSaveEdit={(newContent) => {
                        props.onEdit(absIdx(), newContent)
                        setEditingIndex(null)
                      }}
                      onCancelEdit={() => setEditingIndex(null)}
                      onRemove={() => props.onRemove(absIdx())}
                      onMoveUp={() => {
                        const idx = absIdx()
                        if (idx > 0) props.onReorder(idx, idx - 1)
                      }}
                      onMoveDown={() => {
                        const idx = absIdx()
                        if (idx < props.queuedMessages().length - 1) props.onReorder(idx, idx + 1)
                      }}
                    />
                  )
                }}
              </For>
            </Show>

            {/* Post-complete section */}
            <Show when={postCompleteItems().length > 0}>
              <div class="pt-1 mt-1 border-t border-[var(--border-subtle)]">
                <span class="text-[var(--text-2xs)] font-medium text-[var(--text-muted)] uppercase tracking-wide">
                  Post-complete
                </span>
                <div class="mt-1 space-y-1">
                  <For each={postCompleteItems()}>
                    {(item) => {
                      const absIdx = () => absoluteIndex(item)
                      return (
                        <QueueRow
                          item={item}
                          index={0}
                          total={1}
                          isEditing={editingIndex() === absIdx()}
                          onStartEdit={() => setEditingIndex(absIdx())}
                          onSaveEdit={(newContent) => {
                            props.onEdit(absIdx(), newContent)
                            setEditingIndex(null)
                          }}
                          onCancelEdit={() => setEditingIndex(null)}
                          onRemove={() => props.onRemove(absIdx())}
                          onMoveUp={() => {}}
                          onMoveDown={() => {}}
                        />
                      )
                    }}
                  </For>
                </div>
              </div>
            </Show>
          </div>
        </Show>
      </div>
    </Show>
  )
}
