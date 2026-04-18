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

/**
 * Color-coded by tool type -- matches Pencil design:
 * search/find/git/read = #0A84FF (blue)
 * write/create = #34C759 (green)
 * edit = #0A84FF (blue)
 * bash = #0A84FF (blue), but #FF453A on error
 * delete = #FF453A (red)
 * web = #0A84FF (blue)
 * task/delegate = #5E5CE6 (purple)
 */
export function getToolTypeColor(name: string): string {
  if (['write_file', 'write', 'create_file', 'create'].includes(name)) return 'var(--success)'
  if (['delete_file', 'delete'].includes(name)) return 'var(--error)'
  if (
    [
      'task',
      'delegate_coder',
      'delegate_reviewer',
      'delegate_researcher',
      'delegate_explorer',
    ].includes(name)
  )
    return 'var(--thinking-accent)'
  // read, edit, bash, glob, grep, git, web -- all blue
  return 'var(--accent)'
}

export function getIconColor(status: ToolCallStatus, name?: string): string {
  switch (status) {
    case 'pending':
    case 'running':
      return name ? getToolTypeColor(name) : 'var(--accent)'
    case 'success':
      return name ? getToolTypeColor(name) : 'var(--accent)'
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

function getStatusLabel(status: ToolCallStatus): string {
  switch (status) {
    case 'pending':
      return 'Pending'
    case 'running':
      return 'Running'
    case 'success':
      return 'Complete'
    case 'error':
      return 'Error'
  }
}

export const ToolIcon: Component<ToolIconProps> = (props) => {
  const isRunning = () => props.status === 'running'
  const isPending = () => props.status === 'pending'
  const isError = () => props.status === 'error'
  const baseClass = () => `w-4 h-4 flex-shrink-0 ${props.class ?? ''}`

  return (
    <div class="tool-status-dot-wrapper" title={`${props.name}: ${getStatusLabel(props.status)}`}>
      {/* Pending: clock/waiting indicator */}
      <Show
        when={!isPending()}
        fallback={
          <Dynamic
            component={getToolIcon(props.name)}
            class={baseClass()}
            style={{ color: 'var(--text-muted)', opacity: '0.6' }}
          />
        }
      >
        {/* Running: spinner */}
        <Show
          when={!isRunning()}
          fallback={
            <Loader2 class={`${baseClass()} animate-spin`} style={{ color: 'var(--accent)' }} />
          }
        >
          {/* Error: alert circle */}
          <Show
            when={!isError()}
            fallback={<AlertCircle class={baseClass()} style={{ color: 'var(--error)' }} />}
          >
            {/* Success: tool-specific icon */}
            <Dynamic
              component={getToolIcon(props.name)}
              class={baseClass()}
              style={{ color: getIconColor(props.status, props.name) }}
            />
          </Show>
        </Show>
      </Show>
      {/* Status dot — overlaid top-right */}
      <div class={dotClass(props.status)} aria-hidden="true" />
    </div>
  )
}
