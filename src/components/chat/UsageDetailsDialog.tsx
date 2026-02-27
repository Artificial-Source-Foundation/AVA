import { X } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { formatCost } from '../../lib/cost'
import type { Message, SessionTokenStats } from '../../types'

interface UsageDetailsDialogProps {
  open: boolean
  onClose: () => void
  contextUsage: { used: number; total: number; percentage: number }
  sessionTokenStats: SessionTokenStats
  messages: Message[]
}

const fmt = (n: number) => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

export const UsageDetailsDialog: Component<UsageDetailsDialogProps> = (props) => {
  const rows = createMemo(() =>
    props.messages
      .filter((m) => (m.tokensUsed || 0) > 0 || (m.costUSD || 0) > 0)
      .slice(-12)
      .reverse()
  )

  return (
    <Show when={props.open}>
      <div
        class="fixed inset-0 z-[70] flex items-center justify-center bg-black/60 p-4"
        role="dialog"
        aria-modal="true"
        aria-labelledby="usage-details-title"
        onClick={(event) => {
          if (event.target === event.currentTarget) props.onClose()
        }}
        onKeyDown={(event) => {
          if (event.key === 'Escape') props.onClose()
        }}
      >
        <div class="w-full max-w-2xl rounded-[var(--radius-xl)] border border-[var(--border-default)] bg-[var(--surface-overlay)] shadow-2xl">
          <div class="flex items-center justify-between border-b border-[var(--border-subtle)] px-4 py-3">
            <h3 id="usage-details-title" class="text-sm font-medium text-[var(--text-primary)]">
              Session Usage
            </h3>
            <button
              type="button"
              onClick={props.onClose}
              autofocus
              class="rounded-[var(--radius-sm)] p-1 text-[var(--text-muted)] hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-primary)]"
            >
              <X class="h-4 w-4" />
            </button>
          </div>

          <div class="grid grid-cols-2 gap-2 border-b border-[var(--border-subtle)] px-4 py-3 text-[11px]">
            <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2">
              <p class="text-[var(--text-muted)]">Context</p>
              <p class="text-[var(--text-primary)]">
                {fmt(props.contextUsage.used)} / {fmt(props.contextUsage.total)} (
                {props.contextUsage.percentage.toFixed(0)}%)
              </p>
            </div>
            <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2">
              <p class="text-[var(--text-muted)]">Total Cost</p>
              <p class="text-[var(--text-primary)]">
                {formatCost(props.sessionTokenStats.totalCost)}
              </p>
            </div>
            <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2">
              <p class="text-[var(--text-muted)]">Token Total</p>
              <p class="text-[var(--text-primary)]">{fmt(props.sessionTokenStats.total)}</p>
            </div>
            <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2">
              <p class="text-[var(--text-muted)]">Tokenized Turns</p>
              <p class="text-[var(--text-primary)]">{props.sessionTokenStats.count}</p>
            </div>
          </div>

          <div class="max-h-80 overflow-y-auto px-4 py-3">
            <p class="mb-2 text-[10px] uppercase tracking-wide text-[var(--text-muted)]">
              Recent tokenized turns
            </p>
            <Show
              when={rows().length > 0}
              fallback={<p class="text-[11px] text-[var(--text-muted)]">No tokenized turns yet.</p>}
            >
              <div class="space-y-1">
                <For each={rows()}>
                  {(row) => (
                    <div class="flex items-center justify-between rounded-[var(--radius-sm)] border border-[var(--border-subtle)] px-2.5 py-1.5 text-[11px]">
                      <div class="min-w-0 pr-3">
                        <p class="truncate text-[var(--text-secondary)]">
                          <span class="uppercase text-[var(--text-muted)]">{row.role}</span>{' '}
                          {row.content.replace(/\s+/g, ' ').slice(0, 70)}
                        </p>
                      </div>
                      <div class="flex shrink-0 items-center gap-2 text-[var(--text-muted)]">
                        <span>{fmt(row.tokensUsed || 0)} tok</span>
                        <span>{formatCost(row.costUSD || 0)}</span>
                      </div>
                    </div>
                  )}
                </For>
              </div>
            </Show>
          </div>
        </div>
      </div>
    </Show>
  )
}
