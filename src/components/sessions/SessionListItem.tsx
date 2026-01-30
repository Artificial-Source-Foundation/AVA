/**
 * SessionListItem Component
 * Individual session item in the sidebar with hover actions
 */

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
  const [editName, setEditName] = createSignal(props.session.name)
  let inputRef: HTMLInputElement | undefined

  const handleStartEdit = (e: MouseEvent) => {
    e.stopPropagation()
    setEditName(props.session.name)
    setIsEditing(true)
    // Focus input after render
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
    if (!text) return 'No messages'
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
    <button
      type="button"
      class={`group relative w-full text-left p-3 rounded-lg cursor-pointer transition-colors ${
        props.isActive ? 'bg-blue-600 text-white' : 'hover:bg-gray-700 text-gray-200'
      }`}
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
              class="flex-1 px-2 py-1 bg-gray-600 border border-gray-500 rounded text-white text-sm focus:outline-none focus:border-blue-400"
            />
          </div>
        }
      >
        {/* Normal display */}
        <div class="flex items-start justify-between gap-2">
          <div class="flex-1 min-w-0">
            {/* Session name */}
            <p class="font-medium truncate text-sm">{props.session.name}</p>

            {/* Preview text */}
            <p
              class={`text-xs truncate mt-0.5 ${
                props.isActive ? 'text-blue-100' : 'text-gray-400'
              }`}
            >
              {truncatePreview(props.session.lastPreview)}
            </p>
          </div>

          {/* Hover action buttons */}
          <div
            class={`flex-shrink-0 flex gap-1 ${
              props.isActive ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'
            } transition-opacity`}
          >
            <button
              type="button"
              onClick={handleStartEdit}
              class={`p-1 rounded hover:bg-opacity-20 ${
                props.isActive ? 'hover:bg-white' : 'hover:bg-gray-500'
              }`}
              title="Rename"
            >
              <svg
                class="w-3.5 h-3.5"
                fill="none"
                stroke="currentColor"
                viewBox="0 0 24 24"
                role="img"
                aria-label="Rename"
              >
                <path
                  stroke-linecap="round"
                  stroke-linejoin="round"
                  stroke-width="2"
                  d="M15.232 5.232l3.536 3.536m-2.036-5.036a2.5 2.5 0 113.536 3.536L6.5 21.036H3v-3.572L16.732 3.732z"
                />
              </svg>
            </button>
            <button
              type="button"
              onClick={handleArchive}
              class={`p-1 rounded hover:bg-opacity-20 ${
                props.isActive ? 'hover:bg-white' : 'hover:bg-gray-500'
              }`}
              title="Archive"
            >
              <svg
                class="w-3.5 h-3.5"
                fill="none"
                stroke="currentColor"
                viewBox="0 0 24 24"
                role="img"
                aria-label="Archive"
              >
                <path
                  stroke-linecap="round"
                  stroke-linejoin="round"
                  stroke-width="2"
                  d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"
                />
              </svg>
            </button>
          </div>
        </div>

        {/* Metadata line */}
        <div
          class={`flex items-center gap-2 text-xs mt-1.5 ${
            props.isActive ? 'text-blue-200' : 'text-gray-500'
          }`}
        >
          <span>{props.session.messageCount} msgs</span>
          <span>·</span>
          <span>{formatDate(props.session.updatedAt)}</span>
        </div>
      </Show>
    </button>
  )
}
