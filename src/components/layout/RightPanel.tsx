/**
 * Right Panel — Unified "Inspector" Panel
 *
 * A single scrollable panel showing Changes and Todos sections
 * separated by dividers. Replaces the previous multi-tab layout.
 *
 * Design reference: Pencil node OvbCi / yKUuH
 */

import { Check, FileEdit, FilePlus2, X } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useRustAgent } from '../../hooks/use-rust-agent'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import type { Message, ToolCall } from '../../types'
import type { TodoItem } from '../../types/rust-ipc'
import { PanelErrorBoundary } from '../ui/PanelErrorBoundary'

// ============================================================================
// Changes helpers
// ============================================================================

const FILE_WRITE_TOOLS = new Set(['write', 'edit', 'apply_patch', 'multiedit'])

interface FileChange {
  filePath: string
  fileName: string
  isNew: boolean
  linesAdded: number
  linesRemoved: number
  timestamp: number
}

function resolveFilePath(tc: ToolCall): string | null {
  if (tc.filePath) return tc.filePath
  const a = tc.args
  if (typeof a.path === 'string') return a.path
  if (typeof a.file_path === 'string') return a.file_path
  if (typeof a.filename === 'string') return a.filename
  return null
}

function extractFileChanges(messages: Message[]): FileChange[] {
  const byFile = new Map<string, FileChange>()

  for (const msg of messages) {
    if (!msg.toolCalls) continue
    for (const tc of msg.toolCalls) {
      if (!FILE_WRITE_TOOLS.has(tc.name)) continue
      if (tc.status !== 'success') continue
      const fp = resolveFilePath(tc)
      if (!fp) continue

      const ts = tc.completedAt ?? tc.startedAt
      const existing = byFile.get(fp)

      // Count lines from diff if available
      let added = 0
      let removed = 0
      let isNew = false
      if (tc.diff) {
        const lines = tc.diff.newContent.split('\n')
        const oldLines = tc.diff.oldContent.split('\n')
        added = Math.max(0, lines.length - oldLines.length)
        removed = Math.max(0, oldLines.length - lines.length)
        isNew = !tc.diff.oldContent
      } else if (tc.name === 'write') {
        isNew = true
        added = typeof tc.args.content === 'string' ? tc.args.content.split('\n').length : 0
      }

      if (!existing || ts > existing.timestamp) {
        byFile.set(fp, {
          filePath: fp,
          fileName: fp.split('/').pop() || fp,
          isNew: existing ? existing.isNew : isNew,
          linesAdded: added || existing?.linesAdded || 0,
          linesRemoved: removed || existing?.linesRemoved || 0,
          timestamp: ts,
        })
      }
    }
  }

  return Array.from(byFile.values()).sort((a, b) => b.timestamp - a.timestamp)
}

// ============================================================================
// Section: Activity (removed — only populated in HQ mode; solo mode shows
// tool activity inline in message bubbles instead)
// ============================================================================
// ============================================================================
// Section: Changes
// ============================================================================

const ChangesSection: Component = () => {
  const { messages } = useSession()
  const changes = createMemo(() => extractFileChanges(messages()))

  return (
    <div class="flex flex-col gap-[10px]">
      <span class="inspector-section-title">Changes</span>

      <Show
        when={changes().length > 0}
        fallback={<span class="text-[11px] text-[var(--text-muted)]">No file changes yet</span>}
      >
        <For each={changes()}>
          {(file) => {
            const FileIcon = file.isNew ? FilePlus2 : FileEdit
            const iconColor = file.isNew ? 'var(--success)' : 'var(--warning)'

            return (
              <div class="inspector-card flex items-center justify-between rounded-lg">
                <div class="flex items-center gap-1.5">
                  <FileIcon class="w-[11px] h-[11px] flex-shrink-0" style={{ color: iconColor }} />
                  <span class="truncate font-mono text-[11px] text-[var(--text-secondary)]">
                    {file.fileName}
                  </span>
                </div>
                <div class="flex items-center gap-1 flex-shrink-0">
                  <Show when={file.linesAdded > 0}>
                    <span class="font-mono text-[10px] font-medium text-[var(--success)]">
                      +{file.linesAdded}
                    </span>
                  </Show>
                  <Show when={file.linesRemoved > 0}>
                    <span class="font-mono text-[10px] font-medium text-[var(--error)]">
                      -{file.linesRemoved}
                    </span>
                  </Show>
                  <Show when={file.isNew && file.linesAdded === 0}>
                    <span class="font-mono text-[10px] font-medium text-[var(--success)]">new</span>
                  </Show>
                </div>
              </div>
            )
          }}
        </For>
      </Show>
    </div>
  )
}

// ============================================================================
// Section: Todos
// ============================================================================

const TodosSection: Component = () => {
  const rustAgent = useRustAgent()
  const todos = (): TodoItem[] => rustAgent.todos() ?? []

  // Group: in_progress first, then pending, then completed/cancelled
  const orderedTodos = createMemo(() => {
    const all = todos()
    const inProgress = all.filter((t) => t.status === 'in_progress')
    const pending = all.filter((t) => t.status === 'pending')
    const done = all.filter((t) => t.status === 'completed')
    return [...inProgress, ...pending, ...done]
  })

  return (
    <div class="flex flex-col gap-[10px]">
      <span class="inspector-section-title">Todos</span>

      <Show
        when={orderedTodos().length > 0}
        fallback={<span class="text-[11px] text-[var(--text-muted)]">No todos yet</span>}
      >
        <For each={orderedTodos()}>
          {(item) => {
            const isDone = () => item.status === 'completed'
            return (
              <div class="flex items-start gap-2 w-full">
                {/* Checkbox */}
                <div
                  class="w-4 h-4 rounded flex items-center justify-center flex-shrink-0 mt-[1px]"
                  style={{
                    border: '1.5px solid var(--border-default)',
                  }}
                >
                  <Show when={isDone()}>
                    <Check class="h-[10px] w-[10px] text-[var(--success)]" />
                  </Show>
                </div>
                {/* Text */}
                <span
                  class="text-[12px] leading-[1.4] min-w-0 flex-1"
                  classList={{
                    'text-[var(--text-tertiary)]': isDone(),
                    'text-[var(--text-secondary)]': !isDone(),
                  }}
                >
                  {item.content}
                </span>
              </div>
            )
          }}
        </For>
      </Show>
    </div>
  )
}

// ============================================================================
// Main Component
// ============================================================================

interface RightPanelProps {
  startRightResize: (event: MouseEvent) => void
}

export function RightPanel(props: RightPanelProps) {
  const { settings } = useSettings()
  const { rightPanelVisible, rightPanelWidth, setRightPanelVisible } = useLayout()

  return (
    <Show when={settings().ui.showAgentActivity && rightPanelVisible()}>
      {/* Resize handle */}
      {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
      <div
        class="
          w-[3px] flex-shrink-0 cursor-col-resize
          bg-transparent hover:bg-[var(--accent-muted)]
          active:bg-[var(--accent)]
          transition-colors duration-150
        "
        onMouseDown={(event) => props.startRightResize(event)}
      />

      <div
        class="inspector-panel flex-shrink-0 overflow-hidden"
        style={{ width: `${rightPanelWidth()}px` }}
      >
        <div class="flex flex-col h-full">
          {/* Header — 40px, "Inspector" title + close */}
          <div class="inspector-header">
            <span class="inspector-header-title">Inspector</span>
            <button
              type="button"
              onClick={() => setRightPanelVisible(false)}
              class="flex items-center justify-center text-[var(--text-muted)] transition-colors hover:text-[var(--text-tertiary)]"
              aria-label="Close panel"
              title="Close panel"
            >
              <X class="w-[14px] h-[14px]" />
            </button>
          </div>

          {/* Scrollable content — all three sections */}
          <div class="inspector-content flex-1 overflow-y-auto">
            <PanelErrorBoundary panelName="Inspector">
              {/* Changes */}
              <ChangesSection />

              {/* Divider */}
              <div class="inspector-divider" />

              {/* Todos */}
              <TodosSection />
            </PanelErrorBoundary>
          </div>
        </div>
      </div>
    </Show>
  )
}
