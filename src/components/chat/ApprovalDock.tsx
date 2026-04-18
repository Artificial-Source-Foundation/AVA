/**
 * Approval Dock Component
 *
 * Inline, non-modal tool approval widget that sits between MessageList
 * and MessageInput in the chat area. Card-style design with header bar
 * (amber shield + risk badge), tool name row, command preview card,
 * and right-aligned action buttons.
 *
 * Three explicit action buttons:
 *   Deny         — ghost button                              (outline)
 *   Always Allow — ghost button                              (outline)
 *   Approve      — blue filled                               (primary)
 *
 * Keyboard: Enter = Approve, Shift+Enter = Always Allow, Escape = Deny
 * Accessibility: Focus trapped within dock, initial focus on Approve, live region for announcements
 */

import { Shield, Terminal } from 'lucide-solid'
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
  let dockRef: HTMLDivElement | undefined
  let approveButtonRef: HTMLButtonElement | undefined
  let previouslyFocusedElement: Element | null = null

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

  // Focus management: store previously focused element and focus dock when it appears
  createEffect(() => {
    if (props.request) {
      previouslyFocusedElement = document.activeElement
      // Focus the approve button when dock appears
      requestAnimationFrame(() => {
        approveButtonRef?.focus()
      })
    } else if (previouslyFocusedElement instanceof HTMLElement) {
      // Restore focus when dock closes
      previouslyFocusedElement.focus()
    }
  })

  // Focus trap: keep focus within the dock
  const handleDockKeyDown = (e: KeyboardEvent) => {
    if (e.key !== 'Tab') return

    const focusableElements = dockRef?.querySelectorAll<HTMLElement>(
      'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
    )
    if (!focusableElements || focusableElements.length === 0) return

    const firstElement = focusableElements[0]
    const lastElement = focusableElements[focusableElements.length - 1]

    if (e.shiftKey && document.activeElement === firstElement) {
      e.preventDefault()
      lastElement.focus()
    } else if (!e.shiftKey && document.activeElement === lastElement) {
      e.preventDefault()
      firstElement.focus()
    }
  }

  // Keyboard shortcuts:
  //   Enter           → Approve (only when focus is not on a button; preserves native button activation)
  //   Shift+Enter     → Always Allow (skipped for critical risk; only when focus is not on a button)
  //   Escape          → Deny
  createEffect(() => {
    if (!props.request) return

    const handleKeyDown = (e: KeyboardEvent) => {
      const target = e.target
      // Skip if target is an input/textarea
      if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement) return

      // Escape always denies regardless of focus
      if (e.key === 'Escape') {
        e.preventDefault()
        props.onResolve(false)
        return
      }

      // When focus is on a button, preserve native button activation (don't intercept Enter)
      if (target instanceof HTMLButtonElement) return

      // Global shortcuts only apply when focus is NOT on a button
      if (e.key === 'Enter' && e.shiftKey && riskLevel() !== 'critical') {
        e.preventDefault()
        props.onResolve(true, true)
      } else if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault()
        props.onResolve(true, false)
      }
    }

    document.addEventListener('keydown', handleKeyDown)
    onCleanup(() => document.removeEventListener('keydown', handleKeyDown))
  })

  return (
    <Show when={props.request}>
      {/* Live region for screen reader announcements */}
      <output
        aria-live="polite"
        aria-atomic="true"
        class="sr-only"
        style={{ position: 'absolute', left: '-9999px' }}
      >
        Tool approval request: {props.request?.toolName}. Risk level: {risk().label}. Press Enter to
        approve, Escape to deny
        {riskLevel() !== 'critical' ? ', or Shift Enter to always allow' : ''}.
      </output>
      <div
        ref={dockRef}
        role="dialog"
        aria-label="Tool approval request"
        aria-labelledby="approval-dock-title"
        aria-modal="false"
        class="approval-dock-enter"
        style={{
          width: '620px',
          'max-width': '100%',
          'border-radius': '12px',
          background: 'var(--surface)',
          border: '1px solid var(--border-default)',
          'box-shadow': '0 12px 24px rgba(0, 0, 0, 0.4)',
          overflow: 'hidden',
          'align-self': 'center',
          animation: 'approvalSlideUp 150ms ease-out',
        }}
        onKeyDown={handleDockKeyDown}
      >
        {/* Header bar */}
        <div
          class="flex items-center justify-between"
          style={{
            height: '44px',
            padding: '0 16px',
            background: 'var(--background-subtle)',
          }}
        >
          {/* Left: icon + title */}
          <div class="flex items-center gap-2.5" style={{ height: '100%' }}>
            <Shield class="w-4 h-4" style={{ color: 'var(--warning)' }} />
            <span
              id="approval-dock-title"
              class="text-[13px] font-semibold"
              style={{ color: 'var(--text-primary)' }}
            >
              Tool Approval Required
            </span>
          </div>

          {/* Right: risk badge */}
          <div
            class="flex items-center gap-1.5 px-2 py-0.5"
            style={{
              'border-radius': '8px',
              background: risk().bg,
              color: risk().color,
              'font-size': '11px',
              'font-weight': '500',
            }}
          >
            <Dynamic component={risk().icon} class="w-3 h-3" />
            {risk().label} Risk
          </div>
        </div>

        {/* Body */}
        <div
          style={{
            padding: '16px',
            display: 'flex',
            'flex-direction': 'column',
            gap: '12px',
          }}
        >
          {/* Tool name row */}
          <div class="flex items-center gap-2">
            <Show
              when={toolConfig()}
              fallback={<Terminal class="w-3.5 h-3.5" style={{ color: 'var(--accent)' }} />}
            >
              <Dynamic
                component={toolConfig()!.icon}
                class="w-3.5 h-3.5"
                style={{ color: 'var(--accent)' }}
              />
            </Show>
            <span
              style={{
                color: 'var(--text-primary)',
                'font-family': 'var(--font-mono)',
                'font-size': '12px',
                'font-weight': '600',
              }}
            >
              {props.request!.toolName}
            </span>
          </div>

          {/* Command preview card */}
          <div
            style={{
              padding: '12px',
              'border-radius': '6px',
              background: 'var(--background-subtle)',
              border: '1px solid var(--border-subtle)',
            }}
          >
            <span
              style={{
                color: 'var(--text-secondary)',
                'font-family': 'var(--font-mono)',
                'font-size': '11px',
                'word-break': 'break-all',
                'white-space': 'pre-wrap',
              }}
            >
              {getToolDescription(props.request!.toolName, props.request!.args)}
            </span>
          </div>

          {/* Expanded section */}
          <Show when={expanded()}>
            <ApprovalExpandedDetails request={props.request!} riskLevel={riskLevel()} />
          </Show>

          {/* Action buttons — right-aligned */}
          <div class="flex items-center justify-end gap-2">
            {/* Deny — ghost */}
            <button
              type="button"
              onClick={() => props.onResolve(false)}
              class="inline-flex items-center justify-center transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--surface)]"
              style={{
                padding: '8px 16px',
                'border-radius': '6px',
                background: 'rgba(255, 255, 255, 0.024)',
                border: '1px solid var(--border-default)',
                color: 'var(--text-secondary)',
                'font-size': '12px',
                'font-weight': '500',
                cursor: 'pointer',
              }}
              title="Deny (Esc)"
            >
              Deny
            </button>

            {/* Always Allow — ghost (hidden for critical risk) */}
            <Show when={riskLevel() !== 'critical'}>
              <button
                type="button"
                onClick={() => props.onResolve(true, true)}
                class="inline-flex items-center justify-center transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--surface)]"
                style={{
                  padding: '8px 16px',
                  'border-radius': '6px',
                  background: 'rgba(255, 255, 255, 0.024)',
                  border: '1px solid var(--border-default)',
                  color: 'var(--text-secondary)',
                  'font-size': '12px',
                  'font-weight': '500',
                  cursor: 'pointer',
                }}
                title="Always allow this tool for the session (Shift+Enter)"
              >
                Always Allow
              </button>
            </Show>

            {/* Approve — blue filled */}
            <button
              ref={approveButtonRef}
              type="button"
              onClick={() => props.onResolve(true, false)}
              class="inline-flex items-center justify-center transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2 focus-visible:ring-offset-[var(--surface)]"
              style={{
                padding: '8px 20px',
                'border-radius': '6px',
                background: 'var(--accent)',
                color: 'white',
                'font-size': '12px',
                'font-weight': '600',
                cursor: 'pointer',
                border: 'none',
              }}
              title="Approve this call (Enter)"
            >
              Approve
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}

export default ApprovalDock
