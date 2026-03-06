/**
 * Permission Dialog Component
 *
 * Dialog for requesting user permission for sensitive operations
 * like file writes, command execution, or external API calls.
 */

import { AlertTriangle, Check, Code, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Button } from '../ui/Button'
import { Dialog } from '../ui/Dialog'
import {
  PermissionBadge,
  type PermissionBadgeProps,
  PermissionList,
  type PermissionListProps,
} from './permission/PermissionWidgets'
import {
  type PermissionRequest,
  type PermissionScope,
  type PermissionType,
  permissionConfig,
  riskConfig,
} from './permission/permission-config'

// ============================================================================
// Re-exports for backward compatibility
// ============================================================================

export type { PermissionType, PermissionScope, PermissionRequest }
export type { PermissionBadgeProps, PermissionListProps }
export { PermissionBadge, PermissionList }

// ============================================================================
// Permission Dialog Types
// ============================================================================

export interface PermissionDialogProps {
  /** Whether dialog is open */
  open: boolean
  /** Called when open state changes */
  onOpenChange: (open: boolean) => void
  /** Permission request to display */
  request: PermissionRequest | null
  /** Called when permission is granted */
  onGrant: (scope: PermissionScope) => void
  /** Called when permission is denied */
  onDeny: () => void
}

// ============================================================================
// Permission Dialog Component
// ============================================================================

export const PermissionDialog: Component<PermissionDialogProps> = (props) => {
  const [selectedScope, setSelectedScope] = createSignal<PermissionScope>('once')

  const config = () => (props.request ? permissionConfig[props.request.type] : null)
  const risk = () => (props.request ? riskConfig[props.request.riskLevel] : null)

  const handleGrant = () => {
    props.onGrant(selectedScope())
  }

  const scopes: { id: PermissionScope; label: string; description: string }[] = [
    { id: 'once', label: 'Just this once', description: 'Allow only for this operation' },
    { id: 'session', label: 'For this session', description: 'Allow until the app is closed' },
    { id: 'always', label: 'Always allow', description: 'Remember this permission' },
  ]

  return (
    <Dialog
      open={props.open}
      onOpenChange={props.onOpenChange}
      title="Permission Required"
      size="md"
    >
      <Show when={props.request && config() && risk()}>
        <div class="space-y-5">
          {/* Permission Type Header */}
          <div class="flex items-start gap-4">
            <div class="p-3 rounded-[var(--radius-lg)]" style={{ background: config()!.bg }}>
              <Dynamic
                component={config()!.icon}
                class="w-6 h-6"
                style={{ color: config()!.color }}
              />
            </div>
            <div class="flex-1 min-w-0">
              <h3 class="text-base font-semibold text-[var(--text-primary)]">{config()!.label}</h3>
              <p class="text-sm text-[var(--text-muted)] mt-0.5">{config()!.description}</p>
            </div>
            {/* Risk Badge */}
            <div
              class="flex items-center gap-1.5 px-2 py-1 rounded-full text-xs font-medium"
              style={{
                background: risk()!.bg,
                color: risk()!.color,
              }}
            >
              <Dynamic component={risk()!.icon} class="w-3 h-3" />
              {risk()!.label}
            </div>
          </div>

          {/* Resource Info */}
          <div class="p-3 bg-[var(--surface-sunken)] rounded-[var(--radius-lg)] border border-[var(--border-subtle)]">
            <div class="text-xs text-[var(--text-muted)] mb-1">Resource</div>
            <div class="font-mono text-sm text-[var(--text-primary)] break-all">
              {props.request!.resource}
            </div>
            <Show when={props.request!.description}>
              <div class="text-sm text-[var(--text-secondary)] mt-2">
                {props.request!.description}
              </div>
            </Show>
          </div>

          {/* Command Preview (for command_execute) */}
          <Show when={props.request!.command}>
            <div class="p-3 bg-[var(--surface-sunken)] rounded-[var(--radius-lg)] border border-[var(--border-subtle)]">
              <div class="flex items-center gap-2 text-xs text-[var(--text-muted)] mb-2">
                <Code class="w-3 h-3" />
                Command to execute
              </div>
              <pre class="font-mono text-sm text-[var(--text-primary)] whitespace-pre-wrap break-all">
                {props.request!.command}
              </pre>
            </div>
          </Show>

          {/* Scope Selection */}
          <div>
            <div class="text-sm font-medium text-[var(--text-secondary)] mb-2">
              Permission Scope
            </div>
            <div class="space-y-2">
              <For each={scopes}>
                {(scope) => (
                  <button
                    type="button"
                    onClick={() => setSelectedScope(scope.id)}
                    class={`
                      w-full flex items-center gap-3 p-3
                      border rounded-[var(--radius-lg)]
                      text-left
                      transition-colors duration-[var(--duration-fast)]
                      ${
                        selectedScope() === scope.id
                          ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                          : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                      }
                    `}
                  >
                    <div
                      class={`
                        w-5 h-5 rounded-full border-2 flex items-center justify-center
                        transition-colors duration-[var(--duration-fast)]
                        ${
                          selectedScope() === scope.id
                            ? 'border-[var(--accent)] bg-[var(--accent)]'
                            : 'border-[var(--border-default)]'
                        }
                      `}
                    >
                      <Show when={selectedScope() === scope.id}>
                        <Check class="w-3 h-3 text-white" />
                      </Show>
                    </div>
                    <div class="flex-1">
                      <div
                        class={`text-sm font-medium ${
                          selectedScope() === scope.id
                            ? 'text-[var(--accent)]'
                            : 'text-[var(--text-primary)]'
                        }`}
                      >
                        {scope.label}
                      </div>
                      <div class="text-xs text-[var(--text-muted)]">{scope.description}</div>
                    </div>
                  </button>
                )}
              </For>
            </div>
          </div>

          {/* Warning for high-risk operations */}
          <Show when={props.request!.riskLevel === 'high'}>
            <div class="flex items-start gap-3 p-3 bg-[var(--error-subtle)] border border-[var(--error)] rounded-[var(--radius-lg)]">
              <AlertTriangle class="w-5 h-5 text-[var(--error)] flex-shrink-0 mt-0.5" />
              <p class="text-sm text-[var(--error)]">
                This is a high-risk operation. Please review carefully before granting permission.
              </p>
            </div>
          </Show>

          {/* Actions */}
          <div class="flex items-center justify-end gap-3 pt-2">
            <Button variant="ghost" onClick={props.onDeny} icon={<X class="w-4 h-4" />}>
              Deny
            </Button>
            <Button
              variant={props.request!.riskLevel === 'high' ? 'danger' : 'primary'}
              onClick={handleGrant}
              icon={<Check class="w-4 h-4" />}
            >
              Grant Permission
            </Button>
          </div>
        </div>
      </Show>
    </Dialog>
  )
}
