/**
 * Sidebar Sessions View
 *
 * Session list with search, new chat, and right-click context menu.
 */

import { Copy, MessageSquare, Pencil, Plus, Search, Trash2 } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useNavigation } from '../../stores/navigation'
import { useSession } from '../../stores/session'
import { ContextMenu, type ContextMenuItem } from '../ui/ContextMenu'

interface ContextMenuState {
  x: number
  y: number
  sessionId: string
}

export const SidebarSessions: Component = () => {
  const { goToChat } = useNavigation()
  const {
    sessions,
    currentSession,
    createNewSession,
    switchSession,
    deleteSessionPermanently,
    renameSession,
  } = useSession()
  const [search, setSearch] = createSignal('')
  const [contextMenu, setContextMenu] = createSignal<ContextMenuState | null>(null)
  const [renamingId, setRenamingId] = createSignal<string | null>(null)
  const [renameValue, setRenameValue] = createSignal('')

  const handleNewChat = async () => {
    await createNewSession()
    goToChat()
  }

  const filteredSessions = () => {
    const q = search().toLowerCase()
    if (!q) return sessions()
    return sessions().filter((s) => s.name.toLowerCase().includes(q))
  }

  const formatSessionName = (name: string) => {
    if (name.length > 28) return `${name.slice(0, 28)}...`
    return name
  }

  const formatDate = (timestamp: number) => {
    const date = new Date(timestamp)
    const now = new Date()
    const diff = now.getTime() - date.getTime()
    const days = Math.floor(diff / (1000 * 60 * 60 * 24))

    if (days === 0) return 'Today'
    if (days === 1) return 'Yesterday'
    if (days < 7) return `${days}d ago`
    return date.toLocaleDateString('en-US', { month: 'short', day: 'numeric' })
  }

  const handleContextMenu = (e: MouseEvent, sessionId: string) => {
    e.preventDefault()
    e.stopPropagation()
    setContextMenu({ x: e.clientX, y: e.clientY, sessionId })
  }

  const getContextMenuItems = (sessionId: string): ContextMenuItem[] => {
    return [
      {
        label: 'Rename',
        icon: Pencil,
        action: () => {
          const session = sessions().find((s) => s.id === sessionId)
          if (session) {
            setRenamingId(sessionId)
            setRenameValue(session.name)
          }
        },
      },
      {
        label: 'Duplicate',
        icon: Copy,
        action: () => {
          // Copy session name for a new chat
          const session = sessions().find((s) => s.id === sessionId)
          if (session) {
            navigator.clipboard.writeText(session.name)
          }
        },
      },
      { label: '', action: () => {}, separator: true },
      {
        label: 'Delete',
        icon: Trash2,
        danger: true,
        action: () => {
          deleteSessionPermanently(sessionId)
        },
      },
    ]
  }

  const handleRenameSubmit = (sessionId: string) => {
    const newName = renameValue().trim()
    if (newName && renameSession) {
      renameSession(sessionId, newName)
    }
    setRenamingId(null)
  }

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between px-3 h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
        <span class="text-xs font-semibold text-[var(--text-secondary)] uppercase tracking-wider">
          Sessions
        </span>
        <button
          type="button"
          onClick={handleNewChat}
          class="
            flex items-center justify-center w-6 h-6
            rounded-[var(--radius-md)]
            text-[var(--text-muted)] hover:text-[var(--text-primary)]
            hover:bg-[var(--alpha-white-8)]
            transition-colors
          "
          title="New chat (Ctrl+N)"
        >
          <Plus class="w-4 h-4" />
        </button>
      </div>

      {/* Search */}
      <div class="px-2 py-1.5 flex-shrink-0">
        <div class="relative">
          <Search class="absolute left-2 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search sessions..."
            value={search()}
            onInput={(e) => setSearch(e.currentTarget.value)}
            class="
              w-full pl-7 pr-2 py-1.5
              text-xs text-[var(--text-primary)]
              bg-[var(--surface-sunken)]
              border border-[var(--border-subtle)]
              rounded-[var(--radius-md)]
              placeholder:text-[var(--text-muted)]
              focus-glow
            "
          />
        </div>
      </div>

      {/* Session List */}
      <div class="flex-1 overflow-y-auto px-1.5 scrollbar-none">
        <div class="space-y-0.5">
          <For each={filteredSessions()}>
            {(session) => {
              const isActive = () => currentSession()?.id === session.id
              const isRenaming = () => renamingId() === session.id

              return (
                <Show
                  when={!isRenaming()}
                  fallback={
                    <div class="px-2 py-1">
                      <input
                        type="text"
                        value={renameValue()}
                        onInput={(e) => setRenameValue(e.currentTarget.value)}
                        onKeyDown={(e) => {
                          if (e.key === 'Enter') handleRenameSubmit(session.id)
                          if (e.key === 'Escape') setRenamingId(null)
                        }}
                        onBlur={() => handleRenameSubmit(session.id)}
                        autofocus
                        class="
                          w-full px-2 py-1 text-xs
                          bg-[var(--input-background)]
                          border border-[var(--accent)]
                          rounded-[var(--radius-sm)]
                          text-[var(--text-primary)]
                          focus:outline-none
                        "
                      />
                    </div>
                  }
                >
                  <button
                    type="button"
                    onClick={() => {
                      switchSession(session.id)
                      goToChat()
                    }}
                    onContextMenu={(e) => handleContextMenu(e, session.id)}
                    class={`
                      group flex items-center gap-2 w-full
                      px-2 py-1.5
                      rounded-[var(--radius-md)]
                      text-left transition-colors
                      ${
                        isActive()
                          ? 'bg-[var(--sidebar-item-active)] text-[var(--text-primary)]'
                          : 'text-[var(--text-secondary)] hover:bg-[var(--sidebar-item-hover)] hover:text-[var(--text-primary)]'
                      }
                    `}
                  >
                    <MessageSquare
                      class={`w-3.5 h-3.5 flex-shrink-0 ${isActive() ? 'text-[var(--accent)]' : ''}`}
                    />
                    <div class="flex-1 min-w-0">
                      <div class="text-xs truncate">{formatSessionName(session.name)}</div>
                      <div class="text-[10px] text-[var(--text-muted)] truncate">
                        {formatDate(session.updatedAt)}
                      </div>
                    </div>
                  </button>
                </Show>
              )
            }}
          </For>

          <Show when={filteredSessions().length === 0}>
            <div class="text-center py-8 px-4 text-[var(--text-muted)]">
              <MessageSquare class="w-6 h-6 mx-auto mb-2 opacity-50" />
              <p class="text-xs">{search() ? 'No matching sessions' : 'No chats yet'}</p>
              <Show when={!search()}>
                <p class="text-[10px] mt-1">Start a new conversation</p>
              </Show>
            </div>
          </Show>
        </div>
      </div>

      {/* Context Menu */}
      <Show when={contextMenu()}>
        <ContextMenu
          x={contextMenu()!.x}
          y={contextMenu()!.y}
          items={getContextMenuItems(contextMenu()!.sessionId)}
          onClose={() => setContextMenu(null)}
        />
      </Show>
    </div>
  )
}
