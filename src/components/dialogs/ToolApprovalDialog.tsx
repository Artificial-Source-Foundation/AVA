/**
 * Tool Approval Dialog
 *
 * Modal for approving/denying tool operations during agent execution.
 * Features:
 * - Keyboard shortcuts: Enter = Approve, Escape = Deny
 * - Risk level indicators with color coding
 * - "Always allow this tool" checkbox
 * - Tool arguments preview
 */

import {
  AlertTriangle,
  Check,
  CheckCircle2,
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
import { Button } from '../ui/Button'
import { Checkbox } from '../ui/Checkbox'
import { Dialog } from '../ui/Dialog'

// ============================================================================
// Types
// ============================================================================

export interface ToolApprovalDialogProps {
  /** Approval request to display */
  request: ApprovalRequest | null
  /** Called when user makes a decision */
  onResolve: (approved: boolean, alwaysAllow?: boolean) => void
}

// ============================================================================
// Tool Config
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
    label: 'Low Risk',
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
  },
  medium: {
    icon: ShieldAlert as IconComponent,
    label: 'Medium Risk',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  high: {
    icon: AlertTriangle as IconComponent,
    label: 'High Risk',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
  critical: {
    icon: ShieldX as IconComponent,
    label: 'Critical Risk',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
}

// ============================================================================
// Component
// ============================================================================

export const ToolApprovalDialog: Component<ToolApprovalDialogProps> = (props) => {
  const [alwaysAllow, setAlwaysAllow] = createSignal(false)

  // Get configs
  const toolConfig = () => {
    if (!props.request) return null
    return toolTypeConfig[props.request.type] ?? toolTypeConfig.command
  }

  const riskInfo = () => {
    if (!props.request) return null
    return riskConfig[props.request.riskLevel]
  }

  // Keyboard shortcuts
  createEffect(() => {
    if (!props.request) return

    const handleKeyDown = (e: KeyboardEvent) => {
      // Don't capture if typing in an input
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) {
        return
      }

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

  // Reset always allow when request changes
  createEffect(() => {
    if (props.request) {
      setAlwaysAllow(false)
    }
  })

  // Format args for display
  const formatArgs = () => {
    if (!props.request?.args) return null
    const entries = Object.entries(props.request.args)
    if (entries.length === 0) return null
    return entries
  }

  return (
    <Dialog
      open={!!props.request}
      onOpenChange={(open) => {
        if (!open) props.onResolve(false)
      }}
      title="Tool Approval Required"
      size="md"
    >
      <Show when={props.request && toolConfig() && riskInfo()}>
        <div class="space-y-4">
          {/* Tool Header */}
          <div class="flex items-start gap-4">
            <div class="p-3 rounded-[var(--radius-lg)]" style={{ background: toolConfig()!.bg }}>
              <Dynamic
                component={toolConfig()!.icon}
                class="w-6 h-6"
                style={{ color: toolConfig()!.color }}
              />
            </div>
            <div class="flex-1 min-w-0">
              <div class="flex items-center gap-2">
                <h3 class="text-base font-semibold text-[var(--text-primary)]">
                  {props.request!.toolName}
                </h3>
                {/* Risk Badge */}
                <div
                  class="flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium"
                  style={{
                    background: riskInfo()!.bg,
                    color: riskInfo()!.color,
                  }}
                >
                  <Dynamic component={riskInfo()!.icon} class="w-3 h-3" />
                  {riskInfo()!.label}
                </div>
              </div>
              <p class="text-sm text-[var(--text-muted)] mt-0.5">{props.request!.description}</p>
            </div>
          </div>

          {/* Arguments Preview */}
          <Show when={formatArgs()}>
            <div class="p-3 bg-[var(--surface-sunken)] rounded-[var(--radius-lg)] border border-[var(--border-subtle)]">
              <div class="text-xs text-[var(--text-muted)] mb-2 font-medium">Arguments</div>
              <div class="space-y-1.5">
                <For each={formatArgs()}>
                  {([key, value]) => (
                    <div class="flex gap-2 text-sm">
                      <span class="text-[var(--text-tertiary)] font-mono min-w-[100px]">
                        {key}:
                      </span>
                      <span class="text-[var(--text-primary)] font-mono break-all">
                        {typeof value === 'string'
                          ? value.length > 200
                            ? `${value.slice(0, 200)}...`
                            : value
                          : JSON.stringify(value, null, 2)}
                      </span>
                    </div>
                  )}
                </For>
              </div>
            </div>
          </Show>

          {/* Risk Warning */}
          <Show
            when={props.request!.riskLevel === 'high' || props.request!.riskLevel === 'critical'}
          >
            <div class="flex items-start gap-3 p-3 bg-[var(--error-subtle)] border border-[var(--error)] rounded-[var(--radius-lg)]">
              <AlertTriangle class="w-5 h-5 text-[var(--error)] flex-shrink-0 mt-0.5" />
              <p class="text-sm text-[var(--error)]">
                {props.request!.riskLevel === 'critical'
                  ? 'This is a critical operation that could cause significant changes. Review very carefully.'
                  : 'This is a high-risk operation. Please review the arguments before approving.'}
              </p>
            </div>
          </Show>

          {/* Always Allow Checkbox */}
          <Show when={props.request!.riskLevel !== 'critical'}>
            <div class="flex items-center gap-3">
              <Checkbox id="always-allow" checked={alwaysAllow()} onChange={setAlwaysAllow} />
              <label for="always-allow" class="text-sm text-[var(--text-secondary)] cursor-pointer">
                Always allow <span class="font-medium">{props.request!.toolName}</span> for this
                session
              </label>
            </div>
          </Show>

          {/* Keyboard Hint */}
          <div class="text-xs text-[var(--text-tertiary)] text-center">
            Press{' '}
            <kbd class="px-1.5 py-0.5 bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded text-[10px] font-mono">
              Enter
            </kbd>{' '}
            to approve,{' '}
            <kbd class="px-1.5 py-0.5 bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded text-[10px] font-mono">
              Esc
            </kbd>{' '}
            to deny
          </div>

          {/* Actions */}
          <div class="flex items-center justify-end gap-3 pt-2 border-t border-[var(--border-subtle)]">
            <Button
              variant="ghost"
              onClick={() => props.onResolve(false)}
              icon={<X class="w-4 h-4" />}
            >
              Deny
            </Button>
            <Button
              variant={
                props.request!.riskLevel === 'critical'
                  ? 'danger'
                  : props.request!.riskLevel === 'high'
                    ? 'warning'
                    : 'primary'
              }
              onClick={() => props.onResolve(true, alwaysAllow())}
              icon={<Check class="w-4 h-4" />}
            >
              Approve
            </Button>
          </div>
        </div>
      </Show>
    </Dialog>
  )
}

// ============================================================================
// Export
// ============================================================================

export default ToolApprovalDialog
