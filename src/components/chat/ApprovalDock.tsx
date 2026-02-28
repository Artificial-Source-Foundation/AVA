/**
 * Approval Dock Component
 *
 * Inline, non-modal tool approval widget that sits between MessageList
 * and MessageInput in the chat area. Compact by default (one row),
 * expandable for details. Replaces the old full-screen ToolApprovalDialog.
 *
 * Keyboard: Enter = Approve, Escape = Deny
 */

import {
  AlertTriangle,
  Check,
  CheckCircle2,
  ChevronDown,
  ChevronUp,
  FileEdit,
  Globe,
  Shield,
  ShieldAlert,
  ShieldX,
  Terminal,
  X,
} from 'lucide-solid'
import { type Component, createEffect, createSignal, For, onCleanup, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { ApprovalRequest } from '../../hooks/useAgent'
import { Checkbox } from '../ui/Checkbox'

// ============================================================================
// Types
// ============================================================================

export interface ApprovalDockProps {
  request: ApprovalRequest | null
  onResolve: (approved: boolean, alwaysAllow?: boolean) => void
}

// ============================================================================
// Tool & Risk Config
// ============================================================================

type IconComponent = Component<{ class?: string; style?: { color?: string } }>

interface ToolConfig {
  icon: IconComponent
  label: string
  color: string
  bg: string
}

const toolTypeConfig: Record<string, ToolConfig> = {
  file: {
    icon: FileEdit as IconComponent,
    label: 'File Operation',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  command: {
    icon: Terminal as IconComponent,
    label: 'Shell Command',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  browser: {
    icon: Globe as IconComponent,
    label: 'Browser Action',
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
  mcp: {
    icon: Shield as IconComponent,
    label: 'MCP Tool',
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
}

const riskConfig = {
  low: {
    icon: CheckCircle2 as IconComponent,
    label: 'Low',
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
  },
  medium: {
    icon: ShieldAlert as IconComponent,
    label: 'Medium',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  high: {
    icon: AlertTriangle as IconComponent,
    label: 'High',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
  critical: {
    icon: ShieldX as IconComponent,
    label: 'Critical',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
}

// ============================================================================
// Component
// ============================================================================

export const ApprovalDock: Component<ApprovalDockProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)
  const [alwaysAllow, setAlwaysAllow] = createSignal(false)

  const riskLevel = () => props.request?.riskLevel ?? 'medium'
  const toolConfig = () => {
    if (!props.request) return null
    return toolTypeConfig[props.request.type] ?? toolTypeConfig.command
  }
  const risk = () => riskConfig[riskLevel()]

  // Auto-expand for high/critical risk
  createEffect(() => {
    if (!props.request) {
      setExpanded(false)
      setAlwaysAllow(false)
      return
    }
    const level = riskLevel()
    if (level === 'high' || level === 'critical') {
      setExpanded(true)
    } else {
      setExpanded(false)
    }
    setAlwaysAllow(false)
  })

  // Keyboard shortcuts (Enter=approve, Escape=deny)
  createEffect(() => {
    if (!props.request) return

    const handleKeyDown = (e: KeyboardEvent) => {
      const target = e.target
      if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement) return

      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault()
        props.onResolve(true, alwaysAllow())
      } else if (e.key === 'Escape') {
        e.preventDefault()
        props.onResolve(false)
      }
    }

    document.addEventListener('keydown', handleKeyDown)
    onCleanup(() => document.removeEventListener('keydown', handleKeyDown))
  })

  // Format args for display
  const formatArgs = () => {
    if (!props.request?.args) return null
    const entries = Object.entries(props.request.args)
    if (entries.length === 0) return null
    return entries
  }

  return (
    <Show when={props.request}>
      <div
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

          {/* Tool name */}
          <span class="text-sm font-medium text-[var(--text-primary)] truncate">
            {props.request!.toolName}
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

          {/* Deny */}
          <button
            type="button"
            onClick={() => props.onResolve(false)}
            class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--border-default)] bg-[var(--surface)] px-2 py-1 text-[11px] font-medium text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)] transition-colors"
          >
            <X class="w-3 h-3" />
            Deny
          </button>

          {/* Approve */}
          <button
            type="button"
            onClick={() => props.onResolve(true, alwaysAllow())}
            class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] px-2 py-1 text-[11px] font-medium transition-colors"
            classList={{
              'border border-[var(--error)] text-[var(--error)] hover:bg-[var(--error)] hover:text-white':
                riskLevel() === 'critical',
              'border border-[var(--warning)] text-[var(--warning)] hover:bg-[var(--warning)] hover:text-white':
                riskLevel() === 'high',
              'border border-[var(--success)] text-[var(--success)] hover:bg-[var(--success)] hover:text-white':
                riskLevel() !== 'critical' && riskLevel() !== 'high',
            }}
          >
            <Check class="w-3 h-3" />
            Approve
          </button>
        </div>

        {/* Expanded section */}
        <Show when={expanded()}>
          <div class="px-4 pb-3 space-y-2.5 border-t border-[var(--border-subtle)]">
            {/* Description */}
            <Show when={props.request!.description}>
              <p class="text-xs text-[var(--text-muted)] pt-2">{props.request!.description}</p>
            </Show>

            {/* Arguments preview */}
            <Show when={formatArgs()}>
              <div class="p-2.5 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] border border-[var(--border-subtle)] max-h-40 overflow-y-auto">
                <div class="space-y-1">
                  <For each={formatArgs()}>
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
            <Show when={riskLevel() === 'high' || riskLevel() === 'critical'}>
              <div class="flex items-start gap-2 p-2 bg-[var(--error-subtle)] border border-[var(--error)] rounded-[var(--radius-md)]">
                <AlertTriangle class="w-4 h-4 text-[var(--error)] flex-shrink-0 mt-0.5" />
                <p class="text-xs text-[var(--error)]">
                  {riskLevel() === 'critical'
                    ? 'Critical operation — could cause significant changes. Review carefully.'
                    : 'High-risk operation. Review arguments before approving.'}
                </p>
              </div>
            </Show>

            {/* Always allow + keyboard hints */}
            <div class="flex items-center justify-between pt-1">
              <Show when={riskLevel() !== 'critical'}>
                <div class="flex items-center gap-2">
                  <Checkbox
                    id="dock-always-allow"
                    checked={alwaysAllow()}
                    onChange={setAlwaysAllow}
                  />
                  <label
                    for="dock-always-allow"
                    class="text-[11px] text-[var(--text-secondary)] cursor-pointer"
                  >
                    Always allow <span class="font-medium">{props.request!.toolName}</span> this
                    session
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
        </Show>
      </div>
    </Show>
  )
}

export default ApprovalDock
