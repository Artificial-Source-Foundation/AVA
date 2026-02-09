/**
 * Sidebar Explorer View
 *
 * Project file tree browser. Shows the current project's directory structure.
 * Reads directory contents via Tauri FS plugin with lazy-load expand/collapse.
 */

import { ChevronDown, ChevronRight, File, FolderOpen, FolderTree, Loader2 } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, on, Show } from 'solid-js'
import { type FileEntry, readDirectory } from '../../services/file-browser'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'

// ============================================================================
// File Tree Node
// ============================================================================

const FileTreeNode: Component<{
  entry: FileEntry
  depth: number
  expanded: Set<string>
  onToggle: (path: string) => void
  onFileClick: (path: string) => void
  childrenMap: Map<string, FileEntry[]>
}> = (props) => {
  const isExpanded = () => props.expanded.has(props.entry.path)
  const children = () => props.childrenMap.get(props.entry.path)
  const paddingLeft = () => `${8 + props.depth * 16}px`

  return (
    <>
      <button
        type="button"
        onClick={() => {
          if (props.entry.isDir) {
            props.onToggle(props.entry.path)
          } else {
            props.onFileClick(props.entry.path)
          }
        }}
        class="
          w-full flex items-center gap-1.5 py-0.5 pr-2
          text-xs text-[var(--text-secondary)]
          hover:bg-[var(--alpha-white-05)]
          rounded-sm transition-colors cursor-pointer
          text-left
        "
        style={{ 'padding-left': paddingLeft() }}
      >
        <Show
          when={props.entry.isDir}
          fallback={<File class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />}
        >
          <Show
            when={isExpanded()}
            fallback={<ChevronRight class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />}
          >
            <ChevronDown class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />
          </Show>
        </Show>
        <span class="truncate">{props.entry.name}</span>
      </button>

      {/* Children (recursive) */}
      <Show when={props.entry.isDir && isExpanded() && children()}>
        <For each={children()}>
          {(child) => (
            <FileTreeNode
              entry={child}
              depth={props.depth + 1}
              expanded={props.expanded}
              onToggle={props.onToggle}
              onFileClick={props.onFileClick}
              childrenMap={props.childrenMap}
            />
          )}
        </For>
      </Show>
    </>
  )
}

// ============================================================================
// Explorer Component
// ============================================================================

export const SidebarExplorer: Component = () => {
  const { currentProject } = useProject()
  const { openCodeEditor } = useLayout()

  const [rootEntries, setRootEntries] = createSignal<FileEntry[]>([])
  const [expanded, setExpanded] = createSignal<Set<string>>(new Set())
  const [childrenMap, setChildrenMap] = createSignal<Map<string, FileEntry[]>>(new Map())
  const [loading, setLoading] = createSignal(false)

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
                  childrenMap={childrenMap()}
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
    </div>
  )
}
