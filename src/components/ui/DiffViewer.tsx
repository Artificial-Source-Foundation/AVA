/**
 * Diff Viewer Component
 *
 * Displays code diffs with syntax highlighting and line numbers.
 * Supports unified and side-by-side views.
 */

import { Check, Copy, Minus, Plus, X } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { Button } from './Button'
import { buildSplitPairs, computeDiff, type DiffLine, type SplitPair } from './diff/diff-utils'

// ============================================================================
// Re-exports for backward compatibility
// ============================================================================

export type { DiffLine }
export { computeDiff, buildSplitPairs }

// ============================================================================
// Types
// ============================================================================

export interface DiffViewerProps {
  /** Original content */
  oldContent: string
  /** New content */
  newContent: string
  /** File name */
  filename?: string
  /** View mode */
  mode?: 'unified' | 'split'
  /** Show line numbers */
  showLineNumbers?: boolean
  /** Called when user accepts changes */
  onAccept?: () => void
  /** Called when user rejects changes */
  onReject?: () => void
  /** Additional CSS classes */
  class?: string
}

// ============================================================================
// Diff Viewer Component
// ============================================================================

export const DiffViewer: Component<DiffViewerProps> = (props) => {
  const [copied, setCopied] = createSignal(false)

  const mode = () => props.mode ?? 'unified'
  const showLineNumbers = () => props.showLineNumbers ?? true

  const diffLines = () => computeDiff(props.oldContent, props.newContent)
  const splitPairs = createMemo(() => (mode() === 'split' ? buildSplitPairs(diffLines()) : []))

  const stats = () => {
    const lines = diffLines()
    return {
      additions: lines.filter((l) => l.type === 'add').length,
      deletions: lines.filter((l) => l.type === 'remove').length,
    }
  }

  const copyDiff = async () => {
    const diffText = diffLines()
      .map((line) => {
        const prefix = line.type === 'add' ? '+' : line.type === 'remove' ? '-' : ' '
        return `${prefix} ${line.content}`
      })
      .join('\n')

    await navigator.clipboard.writeText(diffText)
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }

  const lineStyles = {
    add: 'bg-[var(--success-subtle)] text-[var(--success)]',
    remove: 'bg-[var(--error-subtle)] text-[var(--error)]',
    unchanged: 'text-[var(--text-secondary)]',
  }

  const lineNumberStyles = {
    add: 'text-[var(--success)] bg-[var(--success-subtle)]',
    remove: 'text-[var(--error)] bg-[var(--error-subtle)]',
    unchanged: 'text-[var(--text-muted)] bg-[var(--surface-sunken)]',
  }

  return (
    <div
      class={`border border-[var(--border-default)] rounded-[var(--radius-lg)] overflow-hidden ${props.class ?? ''}`}
    >
      {/* Header */}
      <div class="flex items-center justify-between px-4 py-2 bg-[var(--surface-raised)] border-b border-[var(--border-subtle)]">
        <div class="flex items-center gap-3">
          <Show when={props.filename}>
            <span class="text-sm font-medium text-[var(--text-primary)]">{props.filename}</span>
          </Show>
          <div class="flex items-center gap-2 text-xs">
            <span class="flex items-center gap-1 text-[var(--success)]">
              <Plus class="w-3 h-3" />
              {stats().additions}
            </span>
            <span class="flex items-center gap-1 text-[var(--error)]">
              <Minus class="w-3 h-3" />
              {stats().deletions}
            </span>
          </div>
        </div>

        <div class="flex items-center gap-2">
          <Button variant="ghost" size="sm" onClick={copyDiff} icon={<Copy class="w-3.5 h-3.5" />}>
            {copied() ? 'Copied!' : 'Copy'}
          </Button>
          <Show when={props.onReject}>
            <Button
              variant="ghost"
              size="sm"
              onClick={props.onReject}
              icon={<X class="w-3.5 h-3.5" />}
            >
              Reject
            </Button>
          </Show>
          <Show when={props.onAccept}>
            <Button
              variant="success"
              size="sm"
              onClick={props.onAccept}
              icon={<Check class="w-3.5 h-3.5" />}
            >
              Accept
            </Button>
          </Show>
        </div>
      </div>

      {/* Diff Content — Unified */}
      <Show when={mode() === 'unified'}>
        <div class="overflow-x-auto">
          <table class="w-full text-sm font-mono">
            <tbody>
              <For each={diffLines()}>
                {(line) => (
                  <tr class={lineStyles[line.type]}>
                    <Show when={showLineNumbers()}>
                      <td
                        class={`px-2 py-0.5 text-right select-none w-12 border-r border-[var(--border-subtle)] ${lineNumberStyles[line.type]}`}
                      >
                        {line.oldLineNumber ?? ''}
                      </td>
                      <td
                        class={`px-2 py-0.5 text-right select-none w-12 border-r border-[var(--border-subtle)] ${lineNumberStyles[line.type]}`}
                      >
                        {line.newLineNumber ?? ''}
                      </td>
                    </Show>
                    <td
                      class={`px-2 py-0.5 text-center select-none w-6 ${lineNumberStyles[line.type]}`}
                    >
                      {line.type === 'add' ? '+' : line.type === 'remove' ? '-' : ' '}
                    </td>
                    <td class="px-3 py-0.5 whitespace-pre">{line.content || ' '}</td>
                  </tr>
                )}
              </For>
            </tbody>
          </table>
        </div>
      </Show>

      {/* Diff Content — Split (side-by-side) */}
      <Show when={mode() === 'split'}>
        <div class="overflow-x-auto">
          <table class="w-full text-sm font-mono">
            <tbody>
              <For each={splitPairs()}>
                {(pair: SplitPair) => {
                  const leftType = pair.left?.type ?? 'unchanged'
                  const rightType = pair.right?.type ?? 'unchanged'
                  const leftStyle = pair.left ? lineStyles[leftType] : 'bg-[var(--surface-sunken)]'
                  const rightStyle = pair.right
                    ? lineStyles[rightType]
                    : 'bg-[var(--surface-sunken)]'
                  const leftNumStyle = pair.left
                    ? lineNumberStyles[leftType]
                    : 'bg-[var(--surface-sunken)]'
                  const rightNumStyle = pair.right
                    ? lineNumberStyles[rightType]
                    : 'bg-[var(--surface-sunken)]'

                  return (
                    <tr>
                      {/* Left side (old) */}
                      <Show when={showLineNumbers()}>
                        <td
                          class={`px-2 py-0.5 text-right select-none w-10 border-r border-[var(--border-subtle)] ${leftNumStyle}`}
                        >
                          {pair.left?.oldLineNumber ?? ''}
                        </td>
                      </Show>
                      <td class={`px-2 py-0.5 text-center select-none w-5 ${leftNumStyle}`}>
                        {pair.left ? (leftType === 'remove' ? '-' : ' ') : ''}
                      </td>
                      <td class={`px-3 py-0.5 whitespace-pre w-1/2 ${leftStyle}`}>
                        {pair.left?.content || ' '}
                      </td>

                      {/* Divider */}
                      <td class="w-px bg-[var(--border-default)]" />

                      {/* Right side (new) */}
                      <Show when={showLineNumbers()}>
                        <td
                          class={`px-2 py-0.5 text-right select-none w-10 border-r border-[var(--border-subtle)] ${rightNumStyle}`}
                        >
                          {pair.right?.newLineNumber ?? ''}
                        </td>
                      </Show>
                      <td class={`px-2 py-0.5 text-center select-none w-5 ${rightNumStyle}`}>
                        {pair.right ? (rightType === 'add' ? '+' : ' ') : ''}
                      </td>
                      <td class={`px-3 py-0.5 whitespace-pre w-1/2 ${rightStyle}`}>
                        {pair.right?.content || ' '}
                      </td>
                    </tr>
                  )
                }}
              </For>
            </tbody>
          </table>
        </div>
      </Show>

      {/* Empty State */}
      <Show when={diffLines().length === 0}>
        <div class="flex items-center justify-center py-8 text-[var(--text-muted)]">No changes</div>
      </Show>
    </div>
  )
}
