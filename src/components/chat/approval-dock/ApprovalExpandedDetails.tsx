/**
 * Approval Dock Expanded Details
 *
 * Shows description, arguments, and risk warnings when the approval dock is
 * expanded. The always-allow checkbox has been replaced by the top-level
 * "Always Allow" button in ApprovalDock, so it is no longer needed here.
 */

import { AlertTriangle } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { ApprovalRequest } from '../../../hooks/useAgent'

export interface ApprovalExpandedDetailsProps {
  request: ApprovalRequest
  riskLevel: string
}

export const ApprovalExpandedDetails: Component<ApprovalExpandedDetailsProps> = (props) => {
  const formatArgs = (): Array<[string, unknown]> | null => {
    if (!props.request.args) return null
    const entries = Object.entries(props.request.args)
    if (entries.length === 0) return null
    return entries
  }

  return (
    <div class="px-4 pb-3 space-y-2.5 border-t border-[var(--border-subtle)]">
      {/* Description */}
      <Show when={props.request.description}>
        <p class="text-xs text-[var(--text-muted)] pt-2">{props.request.description}</p>
      </Show>

      {/* Arguments preview */}
      <Show when={formatArgs()}>
        <div class="p-2.5 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] border border-[var(--border-subtle)] max-h-40 overflow-y-auto">
          <div class="space-y-1">
            <For each={formatArgs()!}>
              {([key, value]) => (
                <div class="flex gap-2 text-xs">
                  <span class="text-[var(--text-tertiary)] font-mono min-w-[80px] flex-shrink-0">
                    {key}:
                  </span>
                  <span class="text-[var(--text-primary)] font-mono break-all">
                    {typeof value === 'string'
                      ? value.length > 300
                        ? `${value.slice(0, 300)}...`
                        : value
                      : JSON.stringify(value, null, 2)}
                  </span>
                </div>
              )}
            </For>
          </div>
        </div>
      </Show>

      {/* Risk warning (high/critical) */}
      <Show when={props.riskLevel === 'high' || props.riskLevel === 'critical'}>
        <div class="flex items-start gap-2 p-2 bg-[var(--error-subtle)] border border-[var(--error)] rounded-[var(--radius-md)]">
          <AlertTriangle class="w-4 h-4 text-[var(--error)] flex-shrink-0 mt-0.5" />
          <p class="text-xs text-[var(--error)]">
            {props.riskLevel === 'critical'
              ? 'Critical operation — could cause significant changes. Review carefully.'
              : 'High-risk operation. Review arguments before approving.'}
          </p>
        </div>
      </Show>

      {/* Keyboard hints */}
      <div class="flex items-center justify-end pt-1">
        <div class="text-[10px] text-[var(--text-tertiary)]">
          <kbd class="px-1 py-0.5 bg-[var(--surface)] border border-[var(--border-subtle)] rounded text-[9px] font-mono">
            Enter
          </kbd>{' '}
          allow once
          <Show when={props.riskLevel !== 'critical'}>
            {' '}
            <kbd class="px-1 py-0.5 bg-[var(--surface)] border border-[var(--border-subtle)] rounded text-[9px] font-mono">
              Shift+Enter
            </kbd>{' '}
            always allow
          </Show>{' '}
          <kbd class="px-1 py-0.5 bg-[var(--surface)] border border-[var(--border-subtle)] rounded text-[9px] font-mono">
            Esc
          </kbd>{' '}
          deny
        </div>
      </div>
    </div>
  )
}
