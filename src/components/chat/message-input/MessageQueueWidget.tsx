/**
 * Message Queue Widget
 *
 * Renders above the composer textarea when there are queued messages.
 * Supports viewing, inline editing, reordering, and removing queued
 * items. Post-complete messages are shown in a separate section with
 * group numbers.
 *
 * Accessibility: Controls always visible for keyboard users, proper focus indicators,
 * and keyboard shortcuts for common actions.
 */

import { ArrowDown, ArrowUp, Check, ListOrdered, Pencil, X } from 'lucide-solid'
import {
  type Accessor,
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  Show,
} from 'solid-js'

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface QueuedItem {
  id: string
  content: string
  tier: 'queued' | 'interrupt' | 'post-complete' | 'follow-up' | 'steering'
  group?: number
  backendManaged?: boolean
}

export type QueueSection = 'regular' | 'post-complete'

export interface MessageQueueWidgetProps {
  queuedMessages: Accessor<QueuedItem[]>
  onRemove: (index: number, section: QueueSection) => void
  onReorder: (fromIndex: number, toIndex: number, section: QueueSection) => void
  onEdit: (index: number, newContent: string, section: QueueSection) => void
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
  const [draft, setDraft] = createSignal('')

  createEffect(() => {
    setDraft(props.value)
  })

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

  // Auto-focus on mount with focus trap
  queueMicrotask(() => {
    ref?.focus()
    ref?.select()
  })

  return (
    <div class="flex items-start gap-1 flex-1 min-w-0">
      <textarea
        ref={ref}
        value={draft()}
        onInput={(e) => setDraft(e.currentTarget.value)}
        onKeyDown={handleKeyDown}
        rows={1}
        class="flex-1 min-w-0 px-1.5 py-0.5 text-xs bg-[var(--gray-2)] text-[var(--text-primary)] border border-[var(--accent)] rounded resize-none outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)]"
        style={{ 'min-height': '24px', 'max-height': '80px' }}
        aria-label="Edit message content"
        aria-multiline="true"
      />
      <button
        type="button"
        onClick={() => {
          const trimmed = draft().trim()
          if (trimmed) props.onSave(trimmed)
        }}
        class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--accent)] cursor-pointer flex-shrink-0 focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)]"
        title="Save (Enter)"
        aria-label="Save changes"
      >
        <Check class="w-3 h-3" />
      </button>
      <button
        type="button"
        onClick={() => props.onCancel()}
        class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] cursor-pointer flex-shrink-0 focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--error)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)]"
        title="Cancel (Escape)"
        aria-label="Cancel editing"
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
  canMoveUp: boolean
  canMoveDown: boolean
  isEditing: boolean
  onStartEdit: () => void
  onSaveEdit: (newContent: string) => void
  onCancelEdit: () => void
  onRemove: () => void
  onMoveUp: () => void
  onMoveDown: () => void
}

const QueueRow: Component<QueueRowProps> = (props) => {
  const handleKeyDown = (e: KeyboardEvent) => {
    // Only handle keyboard shortcuts when not editing
    if (props.isEditing) return

    switch (e.key) {
      case 'e':
      case 'E':
        if (!props.item.backendManaged) {
          e.preventDefault()
          props.onStartEdit()
        }
        break
      case 'ArrowUp':
        if (props.canMoveUp && e.altKey) {
          e.preventDefault()
          props.onMoveUp()
        }
        break
      case 'ArrowDown':
        if (props.canMoveDown && e.altKey) {
          e.preventDefault()
          props.onMoveDown()
        }
        break
      case 'Delete':
      case 'Backspace':
        if (!props.item.backendManaged && e.altKey) {
          e.preventDefault()
          props.onRemove()
        }
        break
    }
  }

  return (
    <li
      class="flex items-center gap-1.5 py-1 px-2 rounded bg-[var(--alpha-white-5)] group focus-within:bg-[var(--alpha-white-10)]"
      tabindex="0"
      onKeyDown={handleKeyDown}
      aria-label={`${props.item.tier === 'post-complete' ? 'Post-complete' : 'Queued'} message: ${props.item.content.slice(0, 50)}${props.item.content.length > 50 ? '...' : ''}`}
    >
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

      {/* Action buttons — always visible for keyboard accessibility, enhanced on hover/focus */}
      <Show when={!props.isEditing && !props.item.backendManaged}>
        <div
          class="flex items-center gap-0.5 flex-shrink-0"
          role="toolbar"
          aria-label={`Actions for ${props.item.tier === 'post-complete' ? 'post-complete' : 'queued'} message ${props.index + 1}`}
        >
          <button
            type="button"
            onClick={() => props.onStartEdit()}
            class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--accent)] cursor-pointer transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)]"
            title="Edit message (E)"
            aria-label={`Edit ${props.item.tier === 'post-complete' ? 'post-complete' : 'queued'} message ${props.index + 1}`}
          >
            <Pencil class="w-3 h-3" />
          </button>
          <Show when={props.canMoveUp}>
            <button
              type="button"
              onClick={() => props.onMoveUp()}
              class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--text-primary)] cursor-pointer transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)]"
              title="Move up (Arrow Up)"
              aria-label="Move message up in queue"
            >
              <ArrowUp class="w-3 h-3" />
            </button>
          </Show>
          <Show when={props.canMoveDown}>
            <button
              type="button"
              onClick={() => props.onMoveDown()}
              class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--text-primary)] cursor-pointer transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)]"
              title="Move down (Arrow Down)"
              aria-label="Move message down in queue"
            >
              <ArrowDown class="w-3 h-3" />
            </button>
          </Show>
          <button
            type="button"
            onClick={() => props.onRemove()}
            class="p-0.5 rounded hover:bg-[var(--alpha-white-10)] text-[var(--text-muted)] hover:text-[var(--error)] cursor-pointer transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--error)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)]"
            title="Remove (Delete)"
            aria-label={`Remove ${props.item.tier === 'post-complete' ? 'post-complete' : 'queued'} message ${props.index + 1}`}
          >
            <X class="w-3 h-3" />
          </button>
        </div>
      </Show>
    </li>
  )
}

// ---------------------------------------------------------------------------
// Main Widget
// ---------------------------------------------------------------------------

export const MessageQueueWidget: Component<MessageQueueWidgetProps> = (props) => {
  const [expanded, setExpanded] = createSignal(true)
  const [editingRowKey, setEditingRowKey] = createSignal<string | null>(null)

  const count = createMemo(() => props.queuedMessages().length)

  // Split into regular (queued + interrupt) and post-complete
  const regularItems = createMemo(() =>
    props.queuedMessages().filter((m) => m.tier !== 'post-complete')
  )
  const postCompleteItems = createMemo(() =>
    props.queuedMessages().filter((m) => m.tier === 'post-complete')
  )

  // Count of user-managed items (non-backend-managed) for conditional UI
  const userManagedCount = createMemo(
    () => props.queuedMessages().filter((m) => !m.backendManaged).length
  )
  const rowKey = (section: QueueSection, index: number): string => `${section}:${index}`

  return (
    <Show when={count() > 0}>
      <div class="mb-1.5 rounded-lg border border-[var(--border-subtle)] bg-[var(--surface-raised)] overflow-hidden animate-[approvalSlideUp_150ms_ease-out]">
        {/* Compact header */}
        <div class="flex items-center gap-2 px-3 py-1.5">
          <button
            type="button"
            onClick={() => setExpanded((e) => !e)}
            class="flex items-center gap-2 flex-1 min-w-0 text-left cursor-pointer hover:opacity-80 transition-opacity focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)] rounded"
            aria-expanded={expanded()}
            aria-controls="message-queue-list"
            aria-label={`${expanded() ? 'Collapse' : 'Expand'} queued messages (${count()})`}
          >
            <ListOrdered class="w-3.5 h-3.5 text-[var(--accent)] flex-shrink-0" />
            <span class="text-[var(--text-xs)] font-medium text-[var(--text-secondary)]">
              {count()} queued message{count() > 1 ? 's' : ''}
            </span>
            <span class="text-[var(--text-2xs)] text-[var(--text-muted)]" aria-hidden="true">
              {expanded() ? '\u25B4' : '\u25BE'}
            </span>
          </button>

          <Show when={userManagedCount() > 0}>
            <button
              type="button"
              onClick={() => props.onClearAll()}
              class="text-[var(--text-2xs)] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors cursor-pointer px-1.5 py-0.5 rounded hover:bg-[var(--alpha-white-5)] focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--error)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface-raised)]"
              aria-label={`Clear all ${userManagedCount()} local queued messages`}
              title="Clear all local messages (Alt+Shift+Delete)"
            >
              Clear local
            </button>
          </Show>
        </div>

        {/* Expanded message list */}
        <Show when={expanded()}>
          <ul
            id="message-queue-list"
            class="px-3 pb-2 space-y-1 max-h-44 overflow-y-auto scrollbar-thin"
            aria-label={`Queued messages (${count()})`}
          >
            {/* Regular queued messages (queued + interrupt) */}
            <Show when={regularItems().length > 0}>
              <For each={regularItems()}>
                {(item, localIdx) => {
                  return (
                    <QueueRow
                      item={item}
                      index={localIdx()}
                      total={regularItems().length}
                      canMoveUp={
                        localIdx() > 0 && regularItems()[localIdx() - 1]?.backendManaged !== true
                      }
                      canMoveDown={
                        localIdx() < regularItems().length - 1 &&
                        regularItems()[localIdx() + 1]?.backendManaged !== true
                      }
                      isEditing={editingRowKey() === rowKey('regular', localIdx())}
                      onStartEdit={() => setEditingRowKey(rowKey('regular', localIdx()))}
                      onSaveEdit={(newContent) => {
                        props.onEdit(localIdx(), newContent, 'regular')
                        setEditingRowKey(null)
                      }}
                      onCancelEdit={() => setEditingRowKey(null)}
                      onRemove={() => props.onRemove(localIdx(), 'regular')}
                      onMoveUp={() => {
                        if (localIdx() > 0) {
                          props.onReorder(localIdx(), localIdx() - 1, 'regular')
                        }
                      }}
                      onMoveDown={() => {
                        if (localIdx() < regularItems().length - 1) {
                          props.onReorder(localIdx(), localIdx() + 1, 'regular')
                        }
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
                    {(item, localIdx) => {
                      return (
                        <QueueRow
                          item={item}
                          index={localIdx()}
                          total={postCompleteItems().length}
                          canMoveUp={false}
                          canMoveDown={false}
                          isEditing={editingRowKey() === rowKey('post-complete', localIdx())}
                          onStartEdit={() => setEditingRowKey(rowKey('post-complete', localIdx()))}
                          onSaveEdit={(newContent) => {
                            props.onEdit(localIdx(), newContent, 'post-complete')
                            setEditingRowKey(null)
                          }}
                          onCancelEdit={() => setEditingRowKey(null)}
                          onRemove={() => props.onRemove(localIdx(), 'post-complete')}
                          onMoveUp={() => {}}
                          onMoveDown={() => {}}
                        />
                      )
                    }}
                  </For>
                </div>
              </div>
            </Show>
          </ul>
        </Show>
      </div>
    </Show>
  )
}
