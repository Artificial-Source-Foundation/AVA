/**
 * File Tree Component
 *
 * Hierarchical file/folder tree for browsing project directories.
 * Supports expand/collapse, selection, and icons.
 */

import {
  ChevronDown,
  ChevronRight,
  File,
  FileCode,
  FileJson,
  FileText,
  FileType,
  Folder,
  FolderOpen,
  Image,
} from 'lucide-solid'
import { type Component, createSignal, For, type JSX, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'

// ============================================================================
// Types
// ============================================================================

export interface FileTreeNode {
  id: string
  name: string
  type: 'file' | 'folder'
  children?: FileTreeNode[]
  path?: string
}

export interface FileTreeProps {
  /** Tree data */
  data: FileTreeNode[]
  /** Selected node ID */
  selectedId?: string
  /** Expanded folder IDs */
  expandedIds?: string[]
  /** Called when a node is selected */
  onSelect?: (node: FileTreeNode) => void
  /** Called when a folder is expanded/collapsed */
  onToggle?: (node: FileTreeNode, expanded: boolean) => void
  /** Additional CSS classes */
  class?: string
}

// ============================================================================
// File Icon Mapping
// ============================================================================

type IconComponent = (props: { class?: string }) => JSX.Element

const fileIcons: Record<string, IconComponent> = {
  // Code files
  ts: FileCode,
  tsx: FileCode,
  js: FileCode,
  jsx: FileCode,
  py: FileCode,
  rs: FileCode,
  go: FileCode,
  // Data files
  json: FileJson,
  yaml: FileText,
  yml: FileText,
  toml: FileText,
  // Text files
  md: FileText,
  txt: FileText,
  // Image files
  png: Image,
  jpg: Image,
  jpeg: Image,
  gif: Image,
  svg: Image,
  webp: Image,
  // Font files
  ttf: FileType,
  woff: FileType,
  woff2: FileType,
  // Default
  default: File,
}

const getFileIcon = (filename: string): IconComponent => {
  const ext = filename.split('.').pop()?.toLowerCase() || ''
  return fileIcons[ext] || fileIcons.default
}

// ============================================================================
// Tree Node Component
// ============================================================================

interface TreeNodeProps {
  node: FileTreeNode
  level: number
  selectedId?: string
  expandedIds: Set<string>
  onSelect: (node: FileTreeNode) => void
  onToggle: (nodeId: string) => void
}

const TreeNode: Component<TreeNodeProps> = (props) => {
  const isFolder = () => props.node.type === 'folder'
  const isExpanded = () => props.expandedIds.has(props.node.id)
  const isSelected = () => props.selectedId === props.node.id

  const handleClick = () => {
    if (isFolder()) {
      props.onToggle(props.node.id)
    }
    props.onSelect(props.node)
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault()
      handleClick()
    }
    if (e.key === 'ArrowRight' && isFolder() && !isExpanded()) {
      props.onToggle(props.node.id)
    }
    if (e.key === 'ArrowLeft' && isFolder() && isExpanded()) {
      props.onToggle(props.node.id)
    }
  }

  const FileIcon = () => getFileIcon(props.node.name)

  return (
    <div>
      {/* Node Row */}
      <button
        type="button"
        onClick={handleClick}
        onKeyDown={handleKeyDown}
        class={`
          w-full text-left
          flex items-center gap-1.5
          py-1 px-2
          rounded-[var(--radius-md)]
          text-sm
          transition-colors duration-[var(--duration-fast)]
          ${
            isSelected()
              ? 'bg-[var(--accent-subtle)] text-[var(--accent)]'
              : 'text-[var(--text-secondary)] hover:bg-[var(--surface-raised)] hover:text-[var(--text-primary)]'
          }
        `}
        style={{ 'padding-left': `${props.level * 12 + 8}px` }}
      >
        {/* Expand/Collapse Icon (for folders) */}
        <Show when={isFolder()} fallback={<span class="w-4" />}>
          {isExpanded() ? (
            <ChevronDown class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)]" />
          ) : (
            <ChevronRight class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)]" />
          )}
        </Show>

        {/* File/Folder Icon */}
        <Show
          when={isFolder()}
          fallback={
            <Dynamic
              component={FileIcon()}
              class="w-4 h-4 flex-shrink-0 text-[var(--text-tertiary)]"
            />
          }
        >
          {isExpanded() ? (
            <FolderOpen class="w-4 h-4 flex-shrink-0 text-[var(--warning)]" />
          ) : (
            <Folder class="w-4 h-4 flex-shrink-0 text-[var(--warning)]" />
          )}
        </Show>

        {/* Name */}
        <span class="truncate">{props.node.name}</span>
      </button>

      {/* Children (if expanded) */}
      <Show when={isFolder() && isExpanded() && props.node.children}>
        <For each={props.node.children}>
          {(child) => (
            <TreeNode
              node={child}
              level={props.level + 1}
              selectedId={props.selectedId}
              expandedIds={props.expandedIds}
              onSelect={props.onSelect}
              onToggle={props.onToggle}
            />
          )}
        </For>
      </Show>
    </div>
  )
}

// ============================================================================
// File Tree Component
// ============================================================================

export const FileTree: Component<FileTreeProps> = (props) => {
  const [internalExpanded, setInternalExpanded] = createSignal<Set<string>>(
    // eslint-disable-next-line solid/reactivity -- initial value
    new Set(props.expandedIds || [])
  )
  // eslint-disable-next-line solid/reactivity -- initial value
  const [internalSelected, setInternalSelected] = createSignal<string | undefined>(props.selectedId)

  const expandedIds = () => (props.expandedIds ? new Set(props.expandedIds) : internalExpanded())
  const selectedId = () => props.selectedId ?? internalSelected()

  const handleToggle = (nodeId: string) => {
    const newExpanded = new Set(expandedIds())
    const isExpanding = !newExpanded.has(nodeId)

    if (isExpanding) {
      newExpanded.add(nodeId)
    } else {
      newExpanded.delete(nodeId)
    }

    setInternalExpanded(newExpanded)

    // Find the node for the callback
    const findNode = (nodes: FileTreeNode[]): FileTreeNode | undefined => {
      for (const node of nodes) {
        if (node.id === nodeId) return node
        if (node.children) {
          const found = findNode(node.children)
          if (found) return found
        }
      }
      return undefined
    }

    const node = findNode(props.data)
    if (node && props.onToggle) {
      props.onToggle(node, isExpanding)
    }
  }

  const handleSelect = (node: FileTreeNode) => {
    setInternalSelected(node.id)
    props.onSelect?.(node)
  }

  return (
    <div class={`py-1 ${props.class ?? ''}`}>
      <For each={props.data}>
        {(node) => (
          <TreeNode
            node={node}
            level={0}
            selectedId={selectedId()}
            expandedIds={expandedIds()}
            onSelect={handleSelect}
            onToggle={handleToggle}
          />
        )}
      </For>
    </div>
  )
}
