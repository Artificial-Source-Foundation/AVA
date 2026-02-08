/**
 * Session List Item Component
 *
 * Individual session card with hover actions.
 * Premium design with smooth animations.
 */

import { Clock, MessageSquare, Pencil, Trash2 } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import { LIMITS } from '../../config/constants'
import type { SessionWithStats } from '../../types'

interface SessionListItemProps {
  session: SessionWithStats
  isActive: boolean
  onSelect: () => void
  onRename: (name: string) => void
  onArchive: () => void
}

export const SessionListItem: Component<SessionListItemProps> = (props) => {
  const [isEditing, setIsEditing] = createSignal(false)
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [editName, setEditName] = createSignal(props.session.name)
  // oxlint-disable-next-line no-unassigned-vars -- SolidJS ref pattern: assigned via ref={} in JSX
  let inputRef: HTMLInputElement | undefined

  const handleStartEdit = (e: MouseEvent) => {
    e.stopPropagation()
    setEditName(props.session.name)
    setIsEditing(true)
    setTimeout(() => inputRef?.focus(), 0)
  }

  const handleSaveEdit = () => {
    const trimmed = editName().trim()
    if (trimmed && trimmed !== props.session.name) {
      props.onRename(trimmed)
    }
    setIsEditing(false)
  }

  const handleCancelEdit = () => {
    setEditName(props.session.name)
    setIsEditing(false)
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleSaveEdit()
    } else if (e.key === 'Escape') {
      handleCancelEdit()
    }
  }

  const handleArchive = (e: MouseEvent) => {
    e.stopPropagation()
    props.onArchive()
  }

  const formatDate = (timestamp: number): string => {
    const date = new Date(timestamp)
    const now = new Date()
    const diffMs = now.getTime() - date.getTime()
    const diffDays = Math.floor(diffMs / (1000 * 60 * 60 * 24))

    if (diffDays === 0) {
      return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
    } else if (diffDays === 1) {
      return 'Yesterday'
    } else if (diffDays < 7) {
      return date.toLocaleDateString([], { weekday: 'short' })
    } else {
      return date.toLocaleDateString([], { month: 'short', day: 'numeric' })
    }
  }

  const truncatePreview = (text?: string): string => {
    if (!text) return 'No messages yet'
    if (text.length <= LIMITS.MESSAGE_PREVIEW_LENGTH) return text
    return `${text.slice(0, LIMITS.MESSAGE_PREVIEW_LENGTH)}...`
  }

  const handleContainerKeyDown = (e: KeyboardEvent) => {
    if ((e.key === 'Enter' || e.key === ' ') && !isEditing()) {
      e.preventDefault()
      props.onSelect()
    }
  }

  return (
    // biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button (rename/delete inside) which crashes WebKitGTK
    <div
      role="button"
      tabIndex={0}
      class={`
        group relative w-full text-left
        p-3 rounded-[var(--radius-lg)]
        transition-all duration-[var(--duration-fast)]
        ${
          props.isActive
            ? 'bg-[var(--sidebar-item-active)] border border-[var(--accent-muted)]'
            : 'hover:bg-[var(--sidebar-item-hover)] border border-transparent'
        }
      `}
      onClick={() => !isEditing() && props.onSelect()}
      onKeyDown={handleContainerKeyDown}
    >
      <Show
        when={!isEditing()}
        fallback={
          /* Edit mode */
          <div class="flex items-center gap-2">
            <input
              ref={inputRef}
              type="text"
              value={editName()}
              onInput={(e) => setEditName(e.currentTarget.value)}
              onKeyDown={handleKeyDown}
              onBlur={handleSaveEdit}
              maxLength={LIMITS.SESSION_NAME_MAX}
              class="
                flex-1 px-2 py-1.5
                bg-[var(--input-background)]
                border border-[var(--input-border-focus)]
                rounded-[var(--radius-md)]
                text-[var(--text-primary)] text-sm
                focus:outline-none focus:ring-2 focus:ring-[var(--accent-subtle)]
              "
            />
          </div>
        }
      >
        {/* Normal display */}
        <div class="flex items-start justify-between gap-2">
          <div class="flex-1 min-w-0">
            {/* Session name */}
            <p
              class={`
                font-medium text-sm truncate
                $props.isActive ? 'text-[var(--accent)]' : 'text-[var(--text-primary)]'
              `}
            >
              {props.session.name}
            </p>

            {/* Preview text */}
            <p class="text-xs text-[var(--text-tertiary)] truncate mt-1">
              {truncatePreview(props.session.lastPreview)}
            </p>
          </div>

          {/* Hover action buttons */}
          <div
            class={`
              flex-shrink-0 flex gap-0.5
              transition-opacity duration-[var(--duration-fast)]
              $props.isActive ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'
            `}
          >
            <button
              type="button"
              onClick={handleStartEdit}
              class="
                p-1.5 rounded-[var(--radius-md)]
                text-[var(--text-tertiary)]
                hover:text-[var(--text-primary)]
                hover:bg-[var(--surface-raised)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Rename"
            >
              <Pencil class="w-3.5 h-3.5" />
            </button>
            <button
              type="button"
              onClick={handleArchive}
              class="
                p-1.5 rounded-[var(--radius-md)]
                text-[var(--text-tertiary)]
                hover:text-[var(--error)]
                hover:bg-[var(--error-subtle)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Delete"
            >
              <Trash2 class="w-3.5 h-3.5" />
            </button>
          </div>
        </div>

        {/* Metadata line */}
        <div class="flex items-center gap-3 mt-2 text-xs text-[var(--text-muted)]">
          <span class="flex items-center gap-1">
            <MessageSquare class="w-3 h-3" />
            {props.session.messageCount}
          </span>
          <span class="flex items-center gap-1">
            <Clock class="w-3 h-3" />
            {formatDate(props.session.updatedAt)}
          </span>
        </div>
      </Show>
    </div>
  )
}
