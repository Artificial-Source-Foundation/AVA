import { AlertTriangle, Check, X } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import type { ApprovalRequest } from '../../hooks/useAgent'

interface ApprovalStateBarProps {
  request: ApprovalRequest | null
  onApprove: () => void
  onReject: () => void
}

export const ApprovalStateBar: Component<ApprovalStateBarProps> = (props) => {
  return (
    <Show when={props.request}>
      {(request) => (
        <div class="border-b border-[var(--warning)] bg-[var(--warning-subtle)] density-section-px py-2">
          <div class="flex items-center justify-between gap-3">
            <div class="min-w-0 flex items-center gap-2 text-[11px] text-[var(--text-primary)]">
              <AlertTriangle class="h-3.5 w-3.5 shrink-0 text-[var(--warning)]" />
              <span class="truncate">
                Approval required for <span class="font-medium">{request().toolName}</span>
              </span>
            </div>

            <div class="flex shrink-0 items-center gap-1.5">
              <button
                type="button"
                onClick={props.onReject}
                class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--border-default)] bg-[var(--surface-raised)] px-2 py-1 text-[10px] text-[var(--text-secondary)] hover:text-[var(--text-primary)]"
              >
                <X class="h-3 w-3" />
                Reject
              </button>
              <button
                type="button"
                onClick={props.onApprove}
                class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--success)] px-2 py-1 text-[10px] text-[var(--success)] hover:bg-[var(--success)] hover:text-white"
              >
                <Check class="h-3 w-3" />
                Approve
              </button>
            </div>
          </div>
        </div>
      )}
    </Show>
  )
}
