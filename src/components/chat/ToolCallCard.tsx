/**
 * Tool Call Card
 *
 * Shows a single tool call with status icon, name, args summary,
 * duration, and expandable output. Styled per design tokens.
 */

import {
  AlertCircle,
  Check,
  ChevronDown,
  ChevronRight,
  Code2,
  File,
  FileEdit,
  FilePlus,
  FolderSearch,
  Loader2,
  Search,
  Terminal,
  Trash2,
} from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { ToolCall, ToolCallStatus } from '../../types'

// Tool → icon mapping
const TOOL_ICONS: Record<string, Component<{ class?: string }>> = {
  read_file: File,
  write_file: FilePlus,
  create_file: FilePlus,
  edit: FileEdit,
  apply_patch: FileEdit,
  multiedit: FileEdit,
  delete_file: Trash2,
  glob: FolderSearch,
  grep: Search,
  bash: Terminal,
  codesearch: Search,
  ls: FolderSearch,
}

function getToolIcon(name: string): Component<{ class?: string }> {
  return TOOL_ICONS[name] || Code2
}

function getStatusColor(status: ToolCallStatus): string {
  switch (status) {
    case 'pending':
      return 'var(--text-muted)'
    case 'running':
      return 'var(--accent-text)'
    case 'success':
      return 'var(--success)'
    case 'error':
      return 'var(--error)'
  }
}

/** Summarize args into a short string */
function summarizeArgs(name: string, args: Record<string, unknown>): string {
  // File tools: show path
  const path = (args.path ?? args.filePath ?? args.file_path) as string | undefined
  if (path) {
    const short = path.split('/').slice(-2).join('/')
    return short
  }
  // Bash: show command
  if (name === 'bash' && args.command) {
    const cmd = String(args.command)
    return cmd.length > 60 ? `${cmd.slice(0, 57)}...` : cmd
  }
  // Grep: show pattern
  if (name === 'grep' && args.pattern) {
    return `/${args.pattern}/`
  }
  // Glob: show pattern
  if (name === 'glob' && args.pattern) {
    return String(args.pattern)
  }
  return ''
}

/** Format duration in ms to human-readable */
function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  return `${(ms / 1000).toFixed(1)}s`
}

interface ToolCallCardProps {
  toolCall: ToolCall
}

export const ToolCallCard: Component<ToolCallCardProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  const toolIcon = () => getToolIcon(props.toolCall.name)
  const argsSummary = () => summarizeArgs(props.toolCall.name, props.toolCall.args)
  const duration = () => {
    if (!props.toolCall.completedAt) return null
    return formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
  }
  const hasOutput = () => !!(props.toolCall.output || props.toolCall.error)

  return (
    <div class="border border-[var(--border-subtle)] rounded-[var(--radius-md)] overflow-hidden">
      {/* Header row */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        class={`
          flex items-center gap-2 px-2.5 py-1.5
          text-xs cursor-pointer select-none
          hover:bg-[var(--bg-hover)]
          transition-colors duration-[var(--duration-fast)]
          ${expanded() ? 'border-b border-[var(--border-subtle)]' : ''}
        `}
        onClick={() => hasOutput() && setExpanded((v) => !v)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            hasOutput() && setExpanded((v) => !v)
          }
        }}
      >
        {/* Status icon */}
        <Show
          when={props.toolCall.status !== 'running'}
          fallback={
            <Loader2
              class="w-3.5 h-3.5 animate-spin flex-shrink-0"
              style={{ color: getStatusColor('running') }}
            />
          }
        >
          <Show
            when={props.toolCall.status === 'success'}
            fallback={
              <Show
                when={props.toolCall.status === 'error'}
                fallback={
                  <div class="w-3.5 h-3.5 flex-shrink-0 rounded-full border border-[var(--border-muted)]" />
                }
              >
                <AlertCircle
                  class="w-3.5 h-3.5 flex-shrink-0"
                  style={{ color: getStatusColor('error') }}
                />
              </Show>
            }
          >
            <Check class="w-3.5 h-3.5 flex-shrink-0" style={{ color: getStatusColor('success') }} />
          </Show>
        </Show>

        {/* Tool icon + name */}
        <Dynamic
          component={toolIcon()}
          class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)]"
        />
        <span class="font-medium text-[var(--text-primary)] whitespace-nowrap">
          {props.toolCall.name}
        </span>

        {/* Args summary */}
        <Show when={argsSummary()}>
          <span
            class="text-[var(--text-muted)] truncate font-[var(--font-ui-mono)]"
            title={argsSummary()}
          >
            {argsSummary()}
          </span>
        </Show>

        {/* Spacer */}
        <span class="flex-1" />

        {/* Duration */}
        <Show when={duration()}>
          <span class="text-[var(--text-muted)] tabular-nums whitespace-nowrap">{duration()}</span>
        </Show>

        {/* Expand chevron */}
        <Show when={hasOutput()}>
          <Show
            when={expanded()}
            fallback={<ChevronRight class="w-3.5 h-3.5 text-[var(--text-muted)]" />}
          >
            <ChevronDown class="w-3.5 h-3.5 text-[var(--text-muted)]" />
          </Show>
        </Show>
      </div>

      {/* Expanded output */}
      <Show when={expanded() && hasOutput()}>
        <div class="px-2.5 py-2 bg-[var(--bg-inset)] max-h-[200px] overflow-auto">
          <pre class="text-[11px] font-[var(--font-ui-mono)] text-[var(--text-secondary)] whitespace-pre-wrap break-words leading-relaxed">
            {props.toolCall.error || props.toolCall.output}
          </pre>
        </div>
      </Show>
    </div>
  )
}
