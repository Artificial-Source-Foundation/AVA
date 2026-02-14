/**
 * Sidebar Sessions View
 *
 * Session list with search, new chat, and right-click context menu.
 */

import { open } from '@tauri-apps/plugin-dialog'
import {
  Check,
  Compass,
  Copy,
  FolderOpen,
  GitFork,
  MessageSquare,
  Pencil,
  Plus,
  Search,
  Trash2,
  X,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { logError } from '../../services/logger'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import type { ProjectId, ProjectWithStats } from '../../types'
import { ContextMenu, type ContextMenuItem } from '../ui/ContextMenu'

interface ContextMenuState {
  x: number
  y: number
  sessionId: string
}

export const SidebarSessions: Component = () => {
  const {
    sessions,
    currentSession,
    createNewSession,
    switchSession,
    deleteSessionPermanently,
    renameSession,
    duplicateSession,
    forkSession,
    loadSessionsForCurrentProject,
    restoreForCurrentProject,
  } = useSession()
  const { currentProject, favoriteProjects, recentProjects, switchProject, openDirectory } =
    useProject()
  const { openProjectHub, closeProjectHub } = useLayout()
  const [search, setSearch] = createSignal('')
  const [contextMenu, setContextMenu] = createSignal<ContextMenuState | null>(null)
  const [renamingId, setRenamingId] = createSignal<string | null>(null)
  const [renameValue, setRenameValue] = createSignal('')
  const [confirmDeleteId, setConfirmDeleteId] = createSignal<string | null>(null)

  const quickProjects = createMemo(() => {
    const seen = new Set<string>()
    const combined: Array<ProjectWithStats | null> = [
      currentProject() as ProjectWithStats | null,
      ...favoriteProjects(),
      ...recentProjects(),
    ]
    return combined.filter((project): project is ProjectWithStats => {
      if (!project) {
        return false
      }

      if (seen.has(project.id)) {
        return false
      }

      seen.add(project.id)
      return true
    })
  })

  const handleNewChat = async () => {
    await createNewSession()
    closeProjectHub()
  }

  const handleProjectSwitch = async (projectId: string) => {
    if (!projectId || projectId === currentProject()?.id) {
      return
    }

    try {
      await switchProject(projectId as ProjectId)
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      closeProjectHub()
    } catch (err) {
      logError('SidebarSessions', 'Failed to switch project from sidebar', err)
    }
  }

  const handleOpenProject = async () => {
    try {
      const selected = await open({
        directory: true,
        title: 'Select Project Folder',
      })

      if (!selected || typeof selected !== 'string') {
        return
      }

      await openDirectory(selected)
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      closeProjectHub()
    } catch (err) {
      logError('SidebarSessions', 'Failed to open project from sidebar', err)
    }
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
          duplicateSession(sessionId)
        },
      },
      {
        label: 'Fork from here',
        icon: GitFork,
        action: () => {
          const session = sessions().find((s) => s.id === sessionId)
          if (session) {
            forkSession(sessionId, `${session.name} (fork)`)
          }
        },
      },
      { label: '', action: () => {}, separator: true },
      {
        label: 'Delete',
        icon: Trash2,
        danger: true,
        action: () => {
          setConfirmDeleteId(sessionId)
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
      <div class="flex items-center justify-between density-px h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
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
      <div class="density-px density-py flex-shrink-0 border-b border-[var(--border-subtle)] space-y-2">
        <div class="flex items-center gap-1.5">
          <button
            type="button"
            onClick={openProjectHub}
            class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--border-subtle)] px-2 py-1 text-[10px] font-semibold uppercase tracking-wide text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:border-[var(--border-default)]"
            title="Open project hub"
          >
            <Compass class="h-3 w-3" />
            Hub
          </button>

          <button
            type="button"
            onClick={handleOpenProject}
            class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--border-subtle)] px-2 py-1 text-[10px] font-semibold uppercase tracking-wide text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:border-[var(--border-default)]"
            title="Open project folder"
          >
            <FolderOpen class="h-3 w-3" />
            Open
          </button>
        </div>

        <select
          value={currentProject()?.id ?? ''}
          onChange={(e) => {
            void handleProjectSwitch(e.currentTarget.value)
          }}
          class="w-full rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-sunken)] px-2 py-1.5 text-xs text-[var(--text-primary)] focus-glow"
        >
          <option value="" disabled>
            Select project
          </option>
          <For each={quickProjects()}>
            {(project) => (
              <option value={project.id}>
                {project.name}
                {project.isFavorite ? ' *' : ''}
              </option>
            )}
          </For>
        </select>
      </div>

      <div class="density-px density-py flex-shrink-0">
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
              const isConfirmingDelete = () => confirmDeleteId() === session.id

              return (
                <Show
                  when={!isRenaming() && !isConfirmingDelete()}
                  fallback={
                    <Show
                      when={isRenaming()}
                      fallback={
                        /* Delete confirmation row */
                        <div class="flex items-center gap-1.5 density-px density-py rounded-[var(--radius-md)] bg-[var(--error-subtle)] border border-[var(--error)]">
                          <Trash2 class="w-3 h-3 text-[var(--error)] flex-shrink-0" />
                          <span class="text-[10px] text-[var(--error)] flex-1 truncate">
                            Delete session?
                          </span>
                          <button
                            type="button"
                            onClick={() => {
                              deleteSessionPermanently(session.id)
                              setConfirmDeleteId(null)
                            }}
                            class="p-1 rounded-[var(--radius-sm)] text-[var(--error)] hover:bg-[var(--error)] hover:text-white"
                            title="Confirm delete"
                          >
                            <Check class="w-3 h-3" />
                          </button>
                          <button
                            type="button"
                            onClick={() => setConfirmDeleteId(null)}
                            class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]"
                            title="Cancel"
                          >
                            <X class="w-3 h-3" />
                          </button>
                        </div>
                      }
                    >
                      {/* Rename input */}
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
                    </Show>
                  }
                >
                  {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
                  <div
                    role="button"
                    tabIndex={0}
                    onClick={() => switchSession(session.id)}
                    onKeyDown={(e) => {
                      if (e.key === 'Enter' || e.key === ' ') switchSession(session.id)
                    }}
                    onContextMenu={(e) => handleContextMenu(e, session.id)}
                    class={`
                      group flex items-center w-full
                      density-px density-py density-gap
                      rounded-[var(--radius-md)]
                      text-left transition-colors cursor-pointer
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
                      <div class="text-[10px] text-[var(--text-muted)] truncate flex items-center gap-1.5">
                        <span>{formatDate(session.updatedAt)}</span>
                        <Show when={session.messageCount > 0}>
                          <span class="text-[var(--text-muted)]">
                            {session.messageCount} msg{session.messageCount !== 1 ? 's' : ''}
                          </span>
                        </Show>
                      </div>
                    </div>

                    {/* Hover actions — visible on group hover */}
                    <div class="flex items-center gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity flex-shrink-0">
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation()
                          setRenamingId(session.id)
                          setRenameValue(session.name)
                        }}
                        class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]"
                        title="Rename"
                      >
                        <Pencil class="w-3 h-3" />
                      </button>
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation()
                          setConfirmDeleteId(session.id)
                        }}
                        class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--error-subtle)]"
                        title="Delete"
                      >
                        <Trash2 class="w-3 h-3" />
                      </button>
                    </div>
                  </div>
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
