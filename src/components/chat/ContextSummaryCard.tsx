import { ChevronDown, FileText } from 'lucide-solid'
import { type Component, createMemo, Show } from 'solid-js'
import { MarkdownContent } from './MarkdownContent'

interface ContextSummaryCardProps {
  summaryLine: string
  summary: string
  source: 'manual' | 'auto'
  tokensSaved: number
  usageBeforePercent?: number
}

export const ContextSummaryCard: Component<ContextSummaryCardProps> = (props) => {
  const badgeLabel = createMemo(() => (props.source === 'auto' ? 'Automatic' : 'Manual'))
  const headline = createMemo(() => props.summaryLine || 'Conversation compacted')

  return (
    <details class="mb-3 overflow-hidden rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface-raised)]">
      <summary class="flex cursor-pointer list-none items-center gap-3 px-4 py-3 text-left [&::-webkit-details-marker]:hidden">
        <div class="flex h-8 w-8 items-center justify-center rounded-full bg-[var(--accent-subtle)] text-[var(--accent)]">
          <FileText class="h-4 w-4" />
        </div>
        <div class="min-w-0 flex-1">
          <div class="flex items-center gap-2">
            <span class="text-[12px] font-semibold uppercase tracking-[0.08em] text-[var(--text-secondary)]">
              Context Summary
            </span>
            <span class="rounded-full bg-[var(--surface)] px-2 py-0.5 text-[10px] font-medium text-[var(--text-muted)]">
              {badgeLabel()}
            </span>
          </div>
          <p class="mt-1 text-[13px] text-[var(--text-primary)]">{headline()}</p>
          <div class="mt-1 flex items-center gap-2 text-[11px] text-[var(--text-muted)]">
            <span>Saved {props.tokensSaved.toLocaleString()} tokens</span>
            <Show when={props.source === 'auto' && props.usageBeforePercent !== undefined}>
              <span>&middot; Was at {Math.round(props.usageBeforePercent ?? 0)}%</span>
            </Show>
          </div>
        </div>
        <ChevronDown class="h-4 w-4 text-[var(--text-muted)]" />
      </summary>
      <div class="border-t border-[var(--border-subtle)] px-4 py-3">
        <MarkdownContent content={props.summary} messageRole="assistant" isStreaming={false} />
      </div>
    </details>
  )
}
