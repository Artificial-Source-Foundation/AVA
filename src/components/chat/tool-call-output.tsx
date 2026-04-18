/**
 * Tool Call Output
 *
 * Rich output rendering for expanded tool call cards:
 * - Diff view for edit/write tools (delegates to DiffViewer)
 * - Syntax-highlighted output for file/bash tools
 * - Copy button, truncation indicator, enhanced error display
 */

import {
  AlertCircle,
  Check,
  ChevronDown,
  ChevronUp,
  Copy,
  FileX,
  Lock,
  ShieldX,
  Timer,
} from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, on, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { highlightCode } from '../../lib/syntax-highlight'
import type { ToolCall } from '../../types'
import { DiffViewer } from '../ui/DiffViewer'
import { MCPResourceRenderer } from './MCPResourceRenderer'
import { StructuredOutputView } from './StructuredOutputView'
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
  'text-[11px] font-[var(--font-ui-mono)] text-[var(--gray-8)] whitespace-pre-wrap break-words leading-[1.6]'

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

const LINE_LIMIT = 15
const MAX_OUTPUT_LENGTH = 10000 // Character limit for very large outputs

// ============================================================================
// Component
// ============================================================================

interface ToolCallOutputProps {
  toolCall: ToolCall
}

export const ToolCallOutput: Component<ToolCallOutputProps> = (props) => {
  // Structured output: render as JSON tree
  const structuredData = createMemo(() => {
    if (props.toolCall.name !== '__structured_output' || !props.toolCall.output) return null
    try {
      return JSON.parse(props.toolCall.output) as unknown
    } catch {
      return null
    }
  })

  const [copied, setCopied] = createSignal(false)
  const [expanded, setExpanded] = createSignal(false)

  const hasDiff = () =>
    !!(
      props.toolCall.diff?.oldContent !== undefined && props.toolCall.diff?.newContent !== undefined
    )
  const hasError = () => !!props.toolCall.error
  const hasOutput = () => !!props.toolCall.output
  const hasUIResource = () => !!props.toolCall.uiResource

  const errorCategory = () => categorizeToolError(props.toolCall.name, props.toolCall.error)

  const rawOutput = createMemo(() => props.toolCall.output ?? '')
  const outputLines = createMemo(() => rawOutput().split('\n'))
  const totalLineCount = createMemo(() => outputLines().length)
  const totalCharCount = createMemo(() => rawOutput().length)
  const isLong = () => totalLineCount() > LINE_LIMIT || totalCharCount() > MAX_OUTPUT_LENGTH

  const displayOutput = createMemo(() => {
    // When explicitly expanded, show full output without character limits
    if (expanded()) {
      return rawOutput()
    }
    // When collapsed but content is long, apply truncation
    if (isLong()) {
      const lineLimited = outputLines().slice(0, LINE_LIMIT).join('\n')
      // Also apply char limit within line limit for very dense output
      if (lineLimited.length > 5000) {
        return `${lineLimited.slice(0, 5000)}\n[...]`
      }
      return lineLimited
    }
    // Short output: show everything
    return rawOutput()
  })

  const lang = () => detectLanguage(props.toolCall.name, props.toolCall.filePath)

  const highlightedOutput = () => {
    const output = displayOutput()
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
    <Show
      when={structuredData() === null}
      fallback={
        <div class="border-t border-[var(--border-default)]">
          <StructuredOutputView data={structuredData()!} />
        </div>
      }
    >
      <div class="border-t border-[var(--border-default)]">
        {/* Error display */}
        <Show when={hasError()}>
          <div class="bg-[var(--error-subtle)] px-3 py-2.5">
            <div class="flex items-start gap-2">
              <Dynamic
                component={ERROR_ICONS[errorCategory()]}
                class="mt-0.5 h-4 w-4 flex-shrink-0 text-[var(--error)]"
                aria-hidden="true"
              />
              <div class="flex-1 min-w-0">
                <span class="text-[10px] font-semibold uppercase tracking-wider text-[var(--error)]">
                  {getErrorLabel(errorCategory())}
                </span>
                <pre class="mt-1 whitespace-pre-wrap break-words font-[var(--font-ui-mono)] text-[11px] leading-[1.6] text-[var(--error)] opacity-90">
                  {props.toolCall.error}
                </pre>
              </div>
              <button
                type="button"
                onClick={copyContent}
                class="flex-shrink-0 p-1 rounded text-[var(--gray-6)] hover:text-[var(--gray-8)] hover:bg-[var(--alpha-white-5)] transition-colors"
                title="Copy error"
                aria-label="Copy tool error"
              >
                <Show when={copied()} fallback={<Copy class="w-3.5 h-3.5" aria-hidden="true" />}>
                  <Check class="h-3.5 w-3.5 text-[var(--success)]" aria-hidden="true" />
                </Show>
              </button>
            </div>
          </div>
        </Show>

        {/* Diff view for edit/write tools — side-by-side */}
        <Show when={hasDiff() && !hasError()}>
          <section
            class="scroll-fade-mask tool-output-region max-h-[400px] overflow-auto focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-inset"
            data-scrollable
            aria-label={`Diff view for ${props.toolCall.filePath ?? 'file changes'}`}
          >
            <DiffViewer
              oldContent={props.toolCall.diff!.oldContent}
              newContent={props.toolCall.diff!.newContent}
              filename={props.toolCall.filePath}
              mode="split"
              showLineNumbers={true}
              class="border-0 rounded-none"
            />
          </section>
        </Show>

        {/* MCP UI resource rendering (table, chart, form, image, markdown) */}
        <Show when={hasUIResource() && !hasError()}>
          <MCPResourceRenderer resource={props.toolCall.uiResource!} />
        </Show>

        {/* Regular output with syntax highlighting */}
        <Show when={hasOutput() && !hasError() && !hasDiff() && !hasUIResource()}>
          <section
            class={`tool-output-region relative overflow-auto group/output ${expanded() ? 'max-h-[60vh]' : 'max-h-[320px]'}`}
            classList={{ 'scroll-fade-mask': !expanded() }}
            data-scrollable
            aria-label="Tool output"
          >
            {/* Copy button (top-right) */}
            <button
              type="button"
              onClick={copyContent}
              class="absolute top-1.5 right-1.5 z-10 p-1 rounded text-[var(--gray-6)] hover:text-[var(--gray-8)] hover:bg-[var(--alpha-white-8)] transition-colors opacity-0 group-hover/output:opacity-100 focus-visible:opacity-100 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)]"
              title="Copy output"
              aria-label="Copy tool output"
            >
              <Show when={copied()} fallback={<Copy class="w-3.5 h-3.5" aria-hidden="true" />}>
                <Check class="h-3.5 w-3.5 text-[var(--success)]" aria-hidden="true" />
              </Show>
            </button>

            <div class="px-3 py-2 bg-[var(--gray-0)]">
              <HighlightedPre
                output={displayOutput()}
                html={highlightedOutput()}
                hasLang={!!lang()}
              />
            </div>

            {/* Expand/collapse for long output */}
            <Show when={isLong()}>
              <button
                type="button"
                onClick={() => setExpanded((v) => !v)}
                class="flex w-full items-center justify-center gap-2 border-t border-[var(--border-default)] bg-[var(--alpha-white-3)] px-3 py-2 text-center font-[var(--font-ui-mono)] text-[10px] text-[var(--accent)] transition-colors hover:bg-[var(--alpha-white-5)] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-inset"
                aria-label={expanded() ? 'Collapse tool output' : 'Expand tool output'}
              >
                <Show
                  when={expanded()}
                  fallback={
                    <div class="contents">
                      <ChevronDown class="w-3.5 h-3.5" aria-hidden="true" />
                      <span>
                        Show all ({totalLineCount().toLocaleString()} lines,{' '}
                        {(totalCharCount() / 1024).toFixed(1)} KB)
                      </span>
                    </div>
                  }
                >
                  <div class="contents">
                    <ChevronUp class="w-3.5 h-3.5" aria-hidden="true" />
                    <span>Show less</span>
                  </div>
                </Show>
              </button>
            </Show>
          </section>
        </Show>
      </div>
    </Show>
  )
}
