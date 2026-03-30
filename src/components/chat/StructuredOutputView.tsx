/**
 * Structured Output View
 *
 * Collapsible JSON tree component for rendering __structured_output tool results.
 * Shows top-level keys as expandable rows with type-colored values.
 */

import { Check, ChevronDown, ChevronRight, Copy } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, Show } from 'solid-js'

const MAX_DEPTH = 5

interface StructuredOutputViewProps {
  data: unknown
}

const ValueDisplay: Component<{ value: unknown; depth: number }> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  createEffect(() => {
    if (props.depth < 2) {
      setExpanded(true)
    }
  })

  const isObject = () =>
    props.value !== null && typeof props.value === 'object' && !Array.isArray(props.value)
  const isArray = () => Array.isArray(props.value)
  const isExpandable = () => (isObject() || isArray()) && props.depth < MAX_DEPTH

  const colorClass = () => {
    if (props.value === null || props.value === undefined) return 'text-[var(--text-muted)]'
    if (typeof props.value === 'string') return 'text-green-400'
    if (typeof props.value === 'number') return 'text-blue-400'
    if (typeof props.value === 'boolean') return 'text-purple-400'
    return 'text-[var(--text-secondary)]'
  }

  const formatValue = () => {
    if (props.value === null) return 'null'
    if (props.value === undefined) return 'undefined'
    if (typeof props.value === 'string') {
      const v = props.value as string
      return v.length > 80 ? `"${v.slice(0, 77)}..."` : `"${v}"`
    }
    return String(props.value)
  }

  return (
    <Show
      when={isExpandable()}
      fallback={<span class={`text-[11px] font-mono ${colorClass()}`}>{formatValue()}</span>}
    >
      <Show
        when={props.depth >= MAX_DEPTH}
        fallback={
          <div>
            <button
              type="button"
              onClick={() => setExpanded((v) => !v)}
              class="flex items-center gap-0.5 text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
            >
              <Show when={expanded()} fallback={<ChevronRight class="w-3 h-3" />}>
                <ChevronDown class="w-3 h-3" />
              </Show>
              <span class="text-[10px] font-mono opacity-60">
                {isArray()
                  ? `[${(props.value as unknown[]).length}]`
                  : `{${Object.keys(props.value as Record<string, unknown>).length}}`}
              </span>
            </button>
            <Show when={expanded()}>
              <div class="ml-4 border-l border-[var(--border-subtle)] pl-2">
                <For
                  each={
                    isArray()
                      ? (props.value as unknown[]).map((v, i) => [String(i), v] as const)
                      : Object.entries(props.value as Record<string, unknown>)
                  }
                >
                  {([key, val]) => (
                    <div class="flex items-start gap-1.5 py-0.5">
                      <span class="text-[11px] font-mono text-[var(--text-muted)] flex-shrink-0">
                        {key}:
                      </span>
                      <ValueDisplay value={val} depth={props.depth + 1} />
                    </div>
                  )}
                </For>
              </div>
            </Show>
          </div>
        }
      >
        <span class="text-[10px] font-mono text-[var(--text-muted)]">...</span>
      </Show>
    </Show>
  )
}

export const StructuredOutputView: Component<StructuredOutputViewProps> = (props) => {
  const [copied, setCopied] = createSignal(false)

  const copyJson = async () => {
    await navigator.clipboard.writeText(JSON.stringify(props.data, null, 2))
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }

  return (
    <div class="px-3 py-2 bg-[var(--bg-inset,var(--surface-sunken))]">
      <div class="flex items-center justify-between mb-1.5">
        <span class="text-[10px] font-semibold tracking-wider text-[var(--text-muted)] uppercase">
          Structured Output
        </span>
        <button
          type="button"
          onClick={copyJson}
          class="p-1 rounded text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] transition-colors"
          title="Copy JSON"
        >
          <Show when={copied()} fallback={<Copy class="w-3.5 h-3.5" />}>
            <Check class="w-3.5 h-3.5 text-[var(--success)]" />
          </Show>
        </button>
      </div>
      <ValueDisplay value={props.data} depth={0} />
    </div>
  )
}
