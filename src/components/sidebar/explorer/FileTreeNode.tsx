/**
 * File Tree Node Component
 *
 * Recursive tree node for the sidebar explorer.
 * Renders file/directory entries with expand/collapse, change indicators,
 * and read-only markers.
 */

import { ChevronDown, ChevronRight, File, Lock } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { FileEntry } from '../../../services/file-browser'
import type { FileOperationType } from '../../../types'

// ============================================================================
// Constants
// ============================================================================

/** Color for a file operation type indicator dot */
export const changeColor: Record<FileOperationType, string> = {
  write: 'var(--success)',
  edit: 'var(--warning)',
  delete: 'var(--error)',
  read: '', // not shown
}

// ============================================================================
// Component
// ============================================================================

export const FileTreeNode: Component<{
  entry: FileEntry
  depth: number
  expanded: Set<string>
  onToggle: (path: string) => void
  onFileClick: (path: string) => void
  onContextMenu: (e: MouseEvent, path: string, isDir: boolean) => void
  childrenMap: Map<string, FileEntry[]>
  /** Map of file path -> latest operation type (write/edit/delete) */
  changedFiles: Map<string, FileOperationType>
  /** Set of directory paths that contain changed files */
  changedDirs: Set<string>
  /** Set of file paths marked as read-only context */
  readOnlyFiles: Set<string>
}> = (props) => {
  const isExpanded = () => props.expanded.has(props.entry.path)
  const children = () => props.childrenMap.get(props.entry.path)
  const paddingLeft = () => `${8 + props.depth * 16}px`

  const changeType = () => props.changedFiles.get(props.entry.path)
  const dirHasChanges = () => props.entry.isDir && props.changedDirs.has(props.entry.path)
  const isReadOnly = () => !props.entry.isDir && props.readOnlyFiles.has(props.entry.path)

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
        onContextMenu={(e) => props.onContextMenu(e, props.entry.path, props.entry.isDir)}
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
        <span class="truncate flex-1">{props.entry.name}</span>

        {/* Read-only lock icon */}
        <Show when={isReadOnly()}>
          <span title="Read-only context" class="flex-shrink-0">
            <Lock class="w-3 h-3 text-[var(--warning)]" />
          </span>
        </Show>

        {/* Change indicator dot for files */}
        <Show when={!props.entry.isDir && changeType()}>
          <span
            class="w-2 h-2 rounded-full flex-shrink-0"
            style={{ background: changeColor[changeType()!] }}
            title={`${changeType()} during this session`}
          />
        </Show>

        {/* Subtle indicator for directories containing changes */}
        <Show when={props.entry.isDir && dirHasChanges()}>
          <span
            class="w-1.5 h-1.5 rounded-full flex-shrink-0 opacity-60"
            style={{ background: 'var(--accent)' }}
          />
        </Show>
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
              onContextMenu={props.onContextMenu}
              childrenMap={props.childrenMap}
              changedFiles={props.changedFiles}
              changedDirs={props.changedDirs}
              readOnlyFiles={props.readOnlyFiles}
            />
          )}
        </For>
      </Show>
    </>
  )
}
