/**
 * Plugin Detail Panels
 *
 * Permission badges and source info panels shown for the selected plugin.
 */

import { AlertTriangle, FolderSymlink, GitBranch, Shield } from 'lucide-solid'
import { type Accessor, type Component, For, Show } from 'solid-js'
import {
  PLUGIN_PERMISSION_META,
  type PluginCatalogItem,
  type PluginState,
  SENSITIVE_PERMISSIONS,
} from '../../../../types/plugin'
import { permissionColor } from './plugin-utils'

// ---------------------------------------------------------------------------
// Permission Badges
// ---------------------------------------------------------------------------

export interface PluginPermissionBadgesProps {
  plugin: Accessor<PluginCatalogItem | null>
}

export const PluginPermissionBadges: Component<PluginPermissionBadgesProps> = (props) => (
  <Show when={props.plugin()?.permissions && props.plugin()!.permissions!.length > 0}>
    <div class="border border-[var(--border-subtle)] rounded-[var(--radius-md)] bg-[var(--surface)] p-3 space-y-1.5">
      <div class="flex items-center gap-2">
        <Shield class="w-3.5 h-3.5 text-[var(--text-muted)]" />
        <span class="text-[11px] text-[var(--text-primary)]">Permissions</span>
      </div>
      <div class="flex flex-wrap gap-1.5">
        <For each={props.plugin()!.permissions!}>
          {(perm) => {
            const meta = PLUGIN_PERMISSION_META[perm]
            return (
              <span
                class="inline-flex items-center gap-1 px-2 py-0.5 text-[10px] font-medium rounded-full border"
                style={{
                  color: permissionColor(perm),
                  'border-color': permissionColor(perm),
                  'background-color': `color-mix(in srgb, ${permissionColor(perm)} 10%, transparent)`,
                }}
                title={meta?.description ?? ''}
              >
                <Shield class="w-2.5 h-2.5" />
                {meta?.label ?? perm}
              </span>
            )
          }}
        </For>
      </div>
      <Show when={props.plugin()!.permissions!.some((p) => SENSITIVE_PERMISSIONS.includes(p))}>
        <p class="text-[9px] text-[var(--warning)] flex items-center gap-1">
          <AlertTriangle class="w-2.5 h-2.5" />
          This plugin requests sensitive permissions
        </p>
      </Show>
    </div>
  </Show>
)

// ---------------------------------------------------------------------------
// Source Info Panel
// ---------------------------------------------------------------------------

export interface PluginSourceInfoProps {
  plugin: Accessor<PluginCatalogItem | null>
  state: Accessor<PluginState | null>
}

export const PluginSourceInfo: Component<PluginSourceInfoProps> = (props) => (
  <Show
    when={props.plugin() && props.state()?.sourceType && props.state()?.sourceType !== 'catalog'}
  >
    <div class="border border-[var(--border-subtle)] rounded-[var(--radius-md)] bg-[var(--surface)] p-3 space-y-1.5">
      <div class="flex items-center gap-2">
        <Show
          when={props.state()?.sourceType === 'git'}
          fallback={<FolderSymlink class="w-3.5 h-3.5 text-[var(--warning)]" />}
        >
          <GitBranch class="w-3.5 h-3.5 text-[var(--accent)]" />
        </Show>
        <span class="text-[11px] text-[var(--text-primary)]">
          {props.state()?.sourceType === 'git' ? 'Git Source' : 'Local Link'}
        </span>
      </div>
      <Show when={props.state()?.sourceUrl}>
        <p class="text-[10px] text-[var(--text-secondary)] break-all">{props.state()?.sourceUrl}</p>
      </Show>
      <Show when={props.state()?.version}>
        <p class="text-[10px] text-[var(--text-muted)]">Version: {props.state()?.version}</p>
      </Show>
      <p class="text-[10px] text-[var(--text-muted)]">Scope: {props.state()?.scope || 'global'}</p>
    </div>
  </Show>
)
