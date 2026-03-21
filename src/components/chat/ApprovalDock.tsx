/**
 * Approval Dock Component
 *
 * Inline, non-modal tool approval widget that sits between MessageList
 * and MessageInput in the chat area. Compact by default (one row),
 * expandable for details. Replaces the old full-screen ToolApprovalDialog.
 *
 * Three explicit action buttons:
 *   Allow Once   — approve this specific call only        (green outline)
 *   Always Allow — approve this tool for the session     (green filled)
 *   Deny         — reject the call                       (red outline)
 *
 * Keyboard: Enter = Allow Once, Shift+Enter = Always Allow, Escape = Deny
 */

import { Check, CheckCheck, ChevronDown, ChevronUp, X } from 'lucide-solid'
import { type Component, createEffect, createSignal, onCleanup, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { ApprovalRequest } from '../../hooks/useAgent'
import { ApprovalExpandedDetails } from './approval-dock/ApprovalExpandedDetails'
import { riskConfig, toolTypeConfig } from './approval-dock/approval-dock-config'
import { getToolDescription } from './tool-call-utils'

// ============================================================================
// Types
// ============================================================================

export interface ApprovalDockProps {
  request: ApprovalRequest | null
  onResolve: (approved: boolean, alwaysAllow?: boolean) => void
}

// ============================================================================
// Component
// ============================================================================

export const ApprovalDock: Component<ApprovalDockProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  const riskLevel = () => props.request?.riskLevel ?? 'medium'
  const toolConfig = () => {
    if (!props.request) return null
    return toolTypeConfig[props.request.type] ?? toolTypeConfig.command
  }
  const risk = () => riskConfig[riskLevel()]

  // Auto-expand for high/critical risk; reset when request changes
  createEffect(() => {
    if (!props.request) {
      setExpanded(false)
      return
    }
    const level = riskLevel()
    setExpanded(level === 'high' || level === 'critical')
  })

  // Keyboard shortcuts:
  //   Enter           → Allow Once
  //   Shift+Enter     → Always Allow (skipped for critical risk)
  //   Escape          → Deny
  createEffect(() => {
    if (!props.request) return

    const handleKeyDown = (e: KeyboardEvent) => {
      const target = e.target
      if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement) return

      if (e.key === 'Enter' && e.shiftKey && riskLevel() !== 'critical') {
        e.preventDefault()
        props.onResolve(true, true)
      } else if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault()
        props.onResolve(true, false)
      } else if (e.key === 'Escape') {
        e.preventDefault()
        props.onResolve(false)
      }
    }

    document.addEventListener('keydown', handleKeyDown)
    onCleanup(() => document.removeEventListener('keydown', handleKeyDown))
  })

  return (
    <Show when={props.request}>
      <div
        role="dialog"
        aria-label="Tool approval request"
        aria-labelledby="approval-dock-title"
        class="border-t border-b border-[var(--border-subtle)] bg-[var(--surface-raised)] approval-dock-enter"
        style={{ animation: 'approvalSlideUp 150ms ease-out' }}
      >
        {/* Compact row */}
        <div class="flex items-center gap-2.5 px-4 py-2">
          {/* Tool icon */}
          <div
            class="p-1.5 rounded-[var(--radius-md)] flex-shrink-0"
            style={{ background: toolConfig()?.bg }}
          >
            <Show when={toolConfig()}>
              <Dynamic
                component={toolConfig()!.icon}
                class="w-4 h-4"
                style={{ color: toolConfig()!.color }}
              />
            </Show>
          </div>

          {/* Tool description */}
          <span
            id="approval-dock-title"
            class="text-sm font-medium text-[var(--text-primary)] truncate"
          >
            {getToolDescription(props.request!.toolName, props.request!.args)}
          </span>

          {/* Risk badge */}
          <div
            class="flex items-center gap-1 px-1.5 py-0.5 rounded-full text-[10px] font-medium flex-shrink-0"
            style={{ background: risk().bg, color: risk().color }}
          >
            <Dynamic component={risk().icon} class="w-3 h-3" />
            {risk().label}
          </div>

          {/* Spacer */}
          <div class="flex-1" />

          {/* Expand/collapse toggle */}
          <button
            type="button"
            onClick={() => setExpanded(!expanded())}
            class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
            title={expanded() ? 'Collapse details' : 'Expand details'}
          >
            <Show when={expanded()} fallback={<ChevronDown class="w-3.5 h-3.5" />}>
              <ChevronUp class="w-3.5 h-3.5" />
            </Show>
          </button>

          {/* ── Three-button action row ── */}

          {/* Deny — red outline */}
          <button
            type="button"
            onClick={() => props.onResolve(false)}
            class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--error)] px-2 py-1 text-[11px] font-medium text-[var(--error)] hover:bg-[var(--error)] hover:text-white transition-colors"
            title="Deny (Esc)"
          >
            <X class="w-3 h-3" />
            Deny
          </button>

          {/* Allow Once — green outline for normal risk, error colour for critical */}
          <button
            type="button"
            onClick={() => props.onResolve(true, false)}
            class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border px-2 py-1 text-[11px] font-medium transition-colors"
            classList={{
              'border-[var(--error)] text-[var(--error)] hover:bg-[var(--error)] hover:text-white':
                riskLevel() === 'critical',
              'border-[var(--warning)] text-[var(--warning)] hover:bg-[var(--warning)] hover:text-white':
                riskLevel() === 'high',
              'border-[var(--success)] text-[var(--success)] hover:bg-[var(--success)] hover:text-white':
                riskLevel() !== 'critical' && riskLevel() !== 'high',
            }}
            title="Allow for this call only (Enter)"
          >
            <Check class="w-3 h-3" />
            Allow Once
          </button>

          {/* Always Allow — green filled. Hidden for critical risk. */}
          <Show when={riskLevel() !== 'critical'}>
            <button
              type="button"
              onClick={() => props.onResolve(true, true)}
              class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] bg-[var(--success)] px-2 py-1 text-[11px] font-medium text-white hover:opacity-90 transition-opacity"
              title="Always allow this tool for the session (Shift+Enter)"
            >
              <CheckCheck class="w-3 h-3" />
              Always Allow
            </button>
          </Show>
        </div>

        {/* Expanded section */}
        <Show when={expanded()}>
          <ApprovalExpandedDetails request={props.request!} riskLevel={riskLevel()} />
        </Show>
      </div>
    </Show>
  )
}

export default ApprovalDock
