/**
 * Tool Call Output
 *
 * Rich output rendering for expanded tool call cards:
 * - Diff view for edit/write tools (delegates to DiffViewer)
 * - Syntax-highlighted output for file/bash tools
 * - Copy button, truncation indicator, enhanced error display
 */

import { AlertCircle, Check, Copy, FileX, Lock, ShieldX, Timer } from 'lucide-solid'
import { type Component, createEffect, createSignal, on, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { highlightCode } from '../../lib/syntax-highlight'
import type { ToolCall } from '../../types'
import { DiffViewer } from '../ui/DiffViewer'
import {
  categorizeToolError,
  detectLanguage,
  getErrorLabel,
  type ToolErrorCategory,
} from './tool-call-utils'

// ============================================================================
// Error icon mapping
// ============================================================================

const ERROR_ICONS: Record<ToolErrorCategory, Component<{ class?: string }>> = {
  not_found: FileX,
  permission: Lock,
  timeout: Timer,
  execution: AlertCircle,
  denied: ShieldX,
  unknown: AlertCircle,
}

// ============================================================================
// Highlighted <pre> — uses ref + createEffect to avoid innerHTML JSX attribute
// ============================================================================

const PRE_CLASS =
  'text-[11px] font-[var(--font-ui-mono)] text-[var(--text-secondary)] whitespace-pre-wrap break-words leading-relaxed'

const HighlightedPre: Component<{ output: string; html: string; hasLang: boolean }> = (props) => {
  let ref: HTMLPreElement | undefined

  createEffect(
    on(
      () => props.html,
      (html) => {
        if (!ref) return
        if (props.hasLang) {
          ref.innerHTML = html
        } else {
          ref.textContent = props.output
        }
      }
    )
  )

  return <pre ref={ref} class={PRE_CLASS} />
}

// ============================================================================
// Output truncation threshold
// ============================================================================

const TRUNCATION_THRESHOLD = 2000

// ============================================================================
// Component
// ============================================================================

interface ToolCallOutputProps {
  toolCall: ToolCall
}

export const ToolCallOutput: Component<ToolCallOutputProps> = (props) => {
  const [copied, setCopied] = createSignal(false)

  const hasDiff = () =>
    !!(
      props.toolCall.diff?.oldContent !== undefined && props.toolCall.diff?.newContent !== undefined
    )
  const hasError = () => !!props.toolCall.error
  const hasOutput = () => !!props.toolCall.output

  const errorCategory = () => categorizeToolError(props.toolCall.name, props.toolCall.error)
  const isTruncated = () => (props.toolCall.output?.length ?? 0) >= TRUNCATION_THRESHOLD

  const lang = () => detectLanguage(props.toolCall.name, props.toolCall.filePath)

  const highlightedOutput = () => {
    const output = props.toolCall.output
    if (!output) return ''
    const language = lang()
    if (language) return highlightCode(output, language)
    // Escape HTML for plain text
    return output.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  }

  const copyContent = async () => {
    const text = props.toolCall.error || props.toolCall.output || ''
    await navigator.clipboard.writeText(text)
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }

  return (
    <div class="bg-[var(--bg-inset,var(--surface-sunken))] border-t border-[var(--border-subtle)]">
      {/* Error display */}
      <Show when={hasError()}>
        <div class="px-3 py-2.5">
          <div class="flex items-start gap-2">
            <Dynamic
              component={ERROR_ICONS[errorCategory()]}
              class="w-4 h-4 flex-shrink-0 mt-0.5 text-[var(--error)]"
            />
            <div class="flex-1 min-w-0">
              <span class="text-[11px] font-medium text-[var(--error)] uppercase tracking-wider">
                {getErrorLabel(errorCategory())}
              </span>
              <pre class="mt-1 text-[11px] font-[var(--font-ui-mono)] text-[var(--error)] whitespace-pre-wrap break-words leading-relaxed opacity-90">
                {props.toolCall.error}
              </pre>
            </div>
            <button
              type="button"
              onClick={copyContent}
              class="flex-shrink-0 p-1 rounded text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] transition-colors"
              title="Copy error"
            >
              <Show when={copied()} fallback={<Copy class="w-3.5 h-3.5" />}>
                <Check class="w-3.5 h-3.5 text-[var(--success)]" />
              </Show>
            </button>
          </div>
        </div>
      </Show>

      {/* Diff view for edit/write tools */}
      <Show when={hasDiff() && !hasError()}>
        <div class="max-h-[320px] overflow-auto">
          <DiffViewer
            oldContent={props.toolCall.diff!.oldContent}
            newContent={props.toolCall.diff!.newContent}
            filename={props.toolCall.filePath}
            mode="unified"
            showLineNumbers={false}
            class="border-0 rounded-none"
          />
        </div>
      </Show>

      {/* Regular output with syntax highlighting */}
      <Show when={hasOutput() && !hasError() && !hasDiff()}>
        <div class="relative max-h-[320px] overflow-auto">
          {/* Copy button (top-right) */}
          <button
            type="button"
            onClick={copyContent}
            class="absolute top-1.5 right-1.5 z-10 p-1 rounded text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)] transition-colors opacity-0 group-hover/output:opacity-100"
            title="Copy output"
          >
            <Show when={copied()} fallback={<Copy class="w-3.5 h-3.5" />}>
              <Check class="w-3.5 h-3.5 text-[var(--success)]" />
            </Show>
          </button>

          <div class="group/output px-3 py-2">
            <HighlightedPre
              output={props.toolCall.output ?? ''}
              html={highlightedOutput()}
              hasLang={!!lang()}
            />
          </div>

          {/* Truncation indicator */}
          <Show when={isTruncated()}>
            <div class="px-3 py-1.5 text-[10px] text-[var(--text-muted)] bg-[var(--alpha-white-3)] border-t border-[var(--border-subtle)] text-center">
              Output truncated
            </div>
          </Show>
        </div>
      </Show>
    </div>
  )
}
