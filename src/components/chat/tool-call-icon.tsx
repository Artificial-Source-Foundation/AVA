/**
 * Tool Call Icon
 *
 * Status-aware icon for tool calls. Shows spinner when running,
 * AlertCircle when error, tool-specific icon otherwise.
 */

import {
  AlertCircle,
  Code2,
  File,
  FileEdit,
  FilePlus,
  FolderSearch,
  Globe,
  Loader2,
  Search,
  Terminal,
  Trash2,
  Users,
} from 'lucide-solid'
import type { Component } from 'solid-js'
import { Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { ToolCallStatus } from '../../types'

// ============================================================================
// Icon mapping
// ============================================================================

type IconComponent = Component<{ class?: string; style?: Record<string, string> }>

const TOOL_ICONS: Record<string, IconComponent> = {
  read_file: File,
  read: File,
  write_file: FilePlus,
  write: FilePlus,
  create_file: FilePlus,
  create: FilePlus,
  edit: FileEdit,
  apply_patch: FileEdit,
  multiedit: FileEdit,
  delete_file: Trash2,
  delete: Trash2,
  glob: FolderSearch,
  grep: Search,
  bash: Terminal,
  ls: FolderSearch,
  websearch: Globe,
  webfetch: Globe,
  task: Users,
  delegate_coder: Code2,
  delegate_reviewer: Search,
  delegate_researcher: Globe,
  delegate_explorer: FolderSearch,
}

export function getToolIcon(name: string): IconComponent {
  return TOOL_ICONS[name] || Code2
}

export function getIconColor(status: ToolCallStatus): string {
  switch (status) {
    case 'pending':
    case 'running':
      return 'var(--text-muted)'
    case 'success':
      return 'var(--success)'
    case 'error':
      return 'var(--error)'
  }
}

// ============================================================================
// Component
// ============================================================================

interface ToolIconProps {
  name: string
  status: ToolCallStatus
  class?: string
}

function dotClass(status: ToolCallStatus): string {
  switch (status) {
    case 'pending':
      return 'tool-status-dot tool-status-dot--pending'
    case 'running':
      return 'tool-status-dot tool-status-dot--running'
    case 'success':
      return 'tool-status-dot tool-status-dot--success'
    case 'error':
      return 'tool-status-dot tool-status-dot--error'
  }
}

export const ToolIcon: Component<ToolIconProps> = (props) => {
  const isRunning = () => props.status === 'running' || props.status === 'pending'
  const isError = () => props.status === 'error'
  const baseClass = () => `w-4 h-4 flex-shrink-0 ${props.class ?? ''}`

  return (
    <div class="tool-status-dot-wrapper">
      <Show
        when={!isRunning()}
        fallback={<Loader2 class={`${baseClass()} animate-spin text-[var(--accent-text)]`} />}
      >
        <Show
          when={!isError()}
          fallback={<AlertCircle class={baseClass()} style={{ color: 'var(--error)' }} />}
        >
          <Dynamic
            component={getToolIcon(props.name)}
            class={baseClass()}
            style={{ color: getIconColor(props.status) }}
          />
        </Show>
      </Show>
      {/* Status dot — overlaid top-right */}
      <div class={dotClass(props.status)} aria-hidden="true" />
    </div>
  )
}
