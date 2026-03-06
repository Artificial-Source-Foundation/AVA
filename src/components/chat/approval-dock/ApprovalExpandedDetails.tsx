/**
 * Approval Dock Expanded Details
 *
 * Shows description, arguments, risk warnings, always-allow option,
 * and keyboard hints when the approval dock is expanded.
 */

import { AlertTriangle } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { ApprovalRequest } from '../../../hooks/useAgent'
import { Checkbox } from '../../ui/Checkbox'

export interface ApprovalExpandedDetailsProps {
  request: ApprovalRequest
  riskLevel: string
  alwaysAllow: boolean
  onAlwaysAllowChange: (value: boolean) => void
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

      {/* Always allow + keyboard hints */}
      <div class="flex items-center justify-between pt-1">
        <Show when={props.riskLevel !== 'critical'}>
          <div class="flex items-center gap-2">
            <Checkbox
              id="dock-always-allow"
              checked={props.alwaysAllow}
              onChange={props.onAlwaysAllowChange}
            />
            <label
              for="dock-always-allow"
              class="text-[11px] text-[var(--text-secondary)] cursor-pointer"
            >
              Always allow <span class="font-medium">{props.request.toolName}</span> this session
            </label>
          </div>
        </Show>
        <div class="text-[10px] text-[var(--text-tertiary)] ml-auto">
          <kbd class="px-1 py-0.5 bg-[var(--surface)] border border-[var(--border-subtle)] rounded text-[9px] font-mono">
            Enter
          </kbd>{' '}
          approve{' '}
          <kbd class="px-1 py-0.5 bg-[var(--surface)] border border-[var(--border-subtle)] rounded text-[9px] font-mono">
            Esc
          </kbd>{' '}
          deny
        </div>
      </div>
    </div>
  )
}
