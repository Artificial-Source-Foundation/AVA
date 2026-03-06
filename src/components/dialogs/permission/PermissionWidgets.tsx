/**
 * Permission Badge & Permission List Components
 *
 * Reusable widgets for displaying permission status.
 * Extracted from PermissionDialog.tsx to keep each module under 300 lines.
 */

import { Check, X } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { type PermissionScope, type PermissionType, permissionConfig } from './permission-config'

// ============================================================================
// Permission Badge
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
// Permission List
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
