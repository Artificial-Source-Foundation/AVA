/**
 * Permission Dialog Component
 *
 * Dialog for requesting user permission for sensitive operations
 * like file writes, command execution, or external API calls.
 */

import {
  AlertTriangle,
  Check,
  Code,
  FileEdit,
  FolderOpen,
  Globe,
  Shield,
  ShieldAlert,
  Terminal,
  X,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Button } from '../ui/Button'
import { Dialog } from '../ui/Dialog'

// ============================================================================
// Types
// ============================================================================

export type PermissionType =
  | 'file_read'
  | 'file_write'
  | 'file_delete'
  | 'command_execute'
  | 'network_request'
  | 'system_access'

export type PermissionScope = 'once' | 'session' | 'always'

export interface PermissionRequest {
  id: string
  type: PermissionType
  resource: string
  description?: string
  command?: string
  riskLevel: 'low' | 'medium' | 'high'
}

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
// Permission Config
// ============================================================================

type IconComponent = Component<{ class?: string; style?: { color?: string } }>

interface PermissionConfig {
  icon: IconComponent
  label: string
  description: string
  color: string
  bg: string
}

const permissionConfig: Record<PermissionType, PermissionConfig> = {
  file_read: {
    icon: FolderOpen as IconComponent,
    label: 'Read File',
    description: 'Access file contents for analysis',
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
  file_write: {
    icon: FileEdit as IconComponent,
    label: 'Write File',
    description: 'Create or modify file contents',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  file_delete: {
    icon: AlertTriangle as IconComponent,
    label: 'Delete File',
    description: 'Permanently remove files from disk',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
  command_execute: {
    icon: Terminal as IconComponent,
    label: 'Execute Command',
    description: 'Run a shell command',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  network_request: {
    icon: Globe as IconComponent,
    label: 'Network Request',
    description: 'Make an external API call',
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
  system_access: {
    icon: Shield as IconComponent,
    label: 'System Access',
    description: 'Access system resources',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
}

const riskConfig = {
  low: {
    icon: Shield as IconComponent,
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

// ============================================================================
// Permission Badge Component
// ============================================================================

export interface PermissionBadgeProps {
  type: PermissionType
  granted?: boolean
  class?: string
}

export const PermissionBadge: Component<PermissionBadgeProps> = (props) => {
  const config = () => permissionConfig[props.type]

  return (
    <div
      class={`
        inline-flex items-center gap-1.5 px-2 py-1
        rounded-full text-xs font-medium
        ${props.class ?? ''}
      `}
      style={{
        background: props.granted ? 'var(--success-subtle)' : config().bg,
        color: props.granted ? 'var(--success)' : config().color,
      }}
    >
      <Dynamic component={config().icon} class="w-3 h-3" />
      {config().label}
      <Show when={props.granted}>
        <Check class="w-3 h-3" />
      </Show>
    </div>
  )
}

// ============================================================================
// Permission List Component
// ============================================================================

export interface PermissionListProps {
  permissions: Array<{
    type: PermissionType
    resource: string
    scope: PermissionScope
    grantedAt: Date
  }>
  onRevoke?: (type: PermissionType, resource: string) => void
}

export const PermissionList: Component<PermissionListProps> = (props) => {
  return (
    <div class="space-y-2">
      <Show
        when={props.permissions.length > 0}
        fallback={
          <div class="py-8 text-center text-sm text-[var(--text-muted)]">
            No permissions granted yet
          </div>
        }
      >
        <For each={props.permissions}>
          {(perm) => {
            const config = permissionConfig[perm.type]
            return (
              <div class="flex items-center gap-3 p-3 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] border border-[var(--border-subtle)]">
                <div class="p-2 rounded-[var(--radius-md)]" style={{ background: config.bg }}>
                  <Dynamic
                    component={config.icon}
                    class="w-4 h-4"
                    style={{ color: config.color }}
                  />
                </div>
                <div class="flex-1 min-w-0">
                  <div class="text-sm font-medium text-[var(--text-primary)]">{config.label}</div>
                  <div class="text-xs text-[var(--text-muted)] truncate">{perm.resource}</div>
                </div>
                <div class="flex items-center gap-2">
                  <span class="text-xs text-[var(--text-tertiary)] capitalize">{perm.scope}</span>
                  <Show when={props.onRevoke}>
                    <button
                      type="button"
                      onClick={() => props.onRevoke?.(perm.type, perm.resource)}
                      class="
                        p-1.5 text-[var(--text-muted)]
                        hover:text-[var(--error)] hover:bg-[var(--error-subtle)]
                        rounded-[var(--radius-md)]
                        transition-colors duration-[var(--duration-fast)]
                      "
                      title="Revoke permission"
                    >
                      <X class="w-4 h-4" />
                    </button>
                  </Show>
                </div>
              </div>
            )
          }}
        </For>
      </Show>
    </div>
  )
}
