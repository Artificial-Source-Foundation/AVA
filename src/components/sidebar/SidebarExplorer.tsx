/**
 * Sidebar Explorer View
 *
 * Project file tree browser. Shows the current project's directory structure.
 * Reads directory contents via Tauri FS plugin with lazy-load expand/collapse.
 */

import { ExternalLink, File, FolderOpen, FolderTree, Loader2 } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  on,
  onCleanup,
  Show,
} from 'solid-js'
import { type FileEntry, readDirectory } from '../../services/file-browser'
import {
  type EditorInfo,
  getAvailableEditors,
  openInEditor,
  openProjectInEditor,
} from '../../services/ide-integration'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import type { FileOperationType } from '../../types'
import { type ContextMenuState, FileContextMenu } from './explorer/FileContextMenu'
import { FileTreeNode } from './explorer/FileTreeNode'

// ============================================================================
// Explorer Component
// ============================================================================

export const SidebarExplorer: Component = () => {
  const { currentProject } = useProject()
  const { openCodeEditor } = useLayout()
  const session = useSession()

  const [rootEntries, setRootEntries] = createSignal<FileEntry[]>([])
  const [expanded, setExpanded] = createSignal<Set<string>>(new Set())
  const [childrenMap, setChildrenMap] = createSignal<Map<string, FileEntry[]>>(new Map())
  const [loading, setLoading] = createSignal(false)
  const [contextMenu, setContextMenu] = createSignal<ContextMenuState | null>(null)
  const [editors, setEditors] = createSignal<EditorInfo[]>([])

  // Detect available editors once on mount
  void getAvailableEditors().then(setEditors)

  const handleContextMenu = (e: MouseEvent, path: string, isDir: boolean) => {
    e.preventDefault()
    setContextMenu({ x: e.clientX, y: e.clientY, path, isDir })
  }

  const handleOpenIn = (editorCommand: string, filePath: string) => {
    void openInEditor(editorCommand, filePath)
  }

  const handleOpenProjectIn = (editorCommand: string) => {
    const dir = currentProject()?.directory
    if (dir) void openProjectInEditor(editorCommand, dir)
  }

  // Close context menu on Escape
  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Escape' && contextMenu()) {
      setContextMenu(null)
    }
  }
  document.addEventListener('keydown', handleKeyDown)
  onCleanup(() => document.removeEventListener('keydown', handleKeyDown))

  // Build a map of changed files from session file operations
  const changedFiles = createMemo(() => {
    const map = new Map<string, FileOperationType>()
    // Reverse iterate — later operations override earlier ones (latest wins)
    const ops = session.fileOperations()
    for (let i = ops.length - 1; i >= 0; i--) {
      const op = ops[i]!
      if (op.type !== 'read' && !map.has(op.filePath)) {
        map.set(op.filePath, op.type)
      }
    }
    return map
  })

  // Build a set of read-only file paths for quick lookup
  const readOnlyFilesSet = createMemo(() => new Set(session.readOnlyFiles()))

  // Build a set of directory paths that contain changed files
  const changedDirs = createMemo(() => {
    const dirs = new Set<string>()
    for (const filePath of changedFiles().keys()) {
      // Walk up the directory tree
      let dir = filePath
      while (true) {
        const parent = dir.substring(0, dir.lastIndexOf('/'))
        if (!parent || parent === dir) break
        dir = parent
        dirs.add(dir)
      }
    }
    return dirs
  })

  // Load root entries when project directory changes
  createEffect(
    on(
      () => currentProject()?.directory,
      async (dir) => {
        if (!dir) {
          setRootEntries([])
          return
        }
        setLoading(true)
        const entries = await readDirectory(dir)
        setRootEntries(entries)
        setLoading(false)
      }
    )
  )

  // Toggle directory expand/collapse with lazy loading
  const handleToggle = async (path: string) => {
    const current = expanded()
    const next = new Set(current)

    if (next.has(path)) {
      next.delete(path)
    } else {
      next.add(path)
      // Lazy-load children on first expand
      if (!childrenMap().has(path)) {
        const children = await readDirectory(path)
        setChildrenMap((prev) => {
          const m = new Map(prev)
          m.set(path, children)
          return m
        })
      }
    }
    setExpanded(next)
  }

  const handleFileClick = (path: string) => {
    openCodeEditor(path)
  }

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between density-px h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
        <span class="text-xs font-semibold text-[var(--text-secondary)] uppercase tracking-wider">
          Explorer
        </span>
        <Show when={editors().length > 0 && currentProject()?.directory}>
          <button
            type="button"
            onClick={() => handleOpenProjectIn(editors()[0]!.command)}
            class="p-1 rounded hover:bg-[var(--alpha-white-5)] transition-colors cursor-pointer"
            title={`Open project in ${editors()[0]!.name}`}
          >
            <ExternalLink class="w-3.5 h-3.5 text-[var(--text-muted)]" />
          </button>
        </Show>
      </div>

      <Show
        when={currentProject()}
        fallback={
          <div class="flex-1 flex flex-col items-center justify-center px-4 text-center">
            <FolderOpen class="w-8 h-8 text-[var(--text-muted)] mb-3 opacity-50" />
            <p class="text-xs text-[var(--text-muted)] mb-3">No project open</p>
          </div>
        }
      >
        <div class="flex-1 overflow-y-auto px-1 py-1 scrollbar-none">
          {/* Project info */}
          <div class="px-2 py-1.5 mb-1">
            <div class="flex items-center gap-2">
              <FolderTree class="w-3.5 h-3.5 text-[var(--accent)]" />
              <span class="text-xs font-medium text-[var(--text-primary)] truncate">
                {currentProject()?.name}
              </span>
            </div>
            <Show when={currentProject()?.directory}>
              <div class="mt-1 text-[10px] text-[var(--text-muted)] truncate pl-5">
                {currentProject()?.directory}
              </div>
            </Show>
          </div>

          {/* Loading state */}
          <Show when={loading()}>
            <div class="flex items-center justify-center py-4">
              <Loader2 class="w-4 h-4 text-[var(--text-muted)] animate-spin" />
            </div>
          </Show>

          {/* File tree */}
          <Show when={!loading() && rootEntries().length > 0}>
            <For each={rootEntries()}>
              {(entry) => (
                <FileTreeNode
                  entry={entry}
                  depth={0}
                  expanded={expanded()}
                  onToggle={handleToggle}
                  onFileClick={handleFileClick}
                  onContextMenu={handleContextMenu}
                  childrenMap={childrenMap()}
                  changedFiles={changedFiles()}
                  changedDirs={changedDirs()}
                  readOnlyFiles={readOnlyFilesSet()}
                />
              )}
            </For>
          </Show>

          {/* Empty state */}
          <Show when={!loading() && rootEntries().length === 0 && currentProject()?.directory}>
            <div class="px-2 py-4 text-center">
              <File class="w-5 h-5 mx-auto mb-2 text-[var(--text-muted)] opacity-50" />
              <p class="text-[10px] text-[var(--text-muted)]">No files found</p>
            </div>
          </Show>
        </div>
      </Show>

      {/* Right-click context menu */}
      <FileContextMenu
        state={contextMenu()}
        editors={editors()}
        onOpenIn={handleOpenIn}
        onToggleReadOnly={(path) => session.toggleReadOnly(path)}
        isReadOnly={(path) => session.isReadOnly(path)}
        onClose={() => setContextMenu(null)}
      />
    </div>
  )
}
