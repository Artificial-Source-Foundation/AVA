import { type Component, For, Show } from 'solid-js'
import type { PluginCatalogItem, PluginMountRegistration, PluginState } from '../../types/plugin'

interface PluginDetailPanelProps {
  plugin: PluginCatalogItem | null
  state: PluginState | null
  mounts?: PluginMountRegistration[]
}

export const PluginDetailPanel: Component<PluginDetailPanelProps> = (props) => {
  return (
    <div class="border border-[var(--border-subtle)] rounded-[var(--radius-md)] bg-[var(--surface)] p-3">
      <Show
        when={props.plugin}
        fallback={
          <p class="text-[11px] text-[var(--text-muted)]">Select a plugin to view details.</p>
        }
      >
        {(plugin) => (
          <div class="space-y-1.5">
            <div class="flex items-center justify-between gap-2">
              <h4 class="text-xs text-[var(--text-primary)]">{plugin().name}</h4>
              <span class="text-[10px] px-2 py-0.5 rounded-[var(--radius-sm)] border border-[var(--border-subtle)] text-[var(--text-muted)] uppercase tracking-wide">
                {plugin().category}
              </span>
            </div>
            <p class="text-[11px] text-[var(--text-secondary)]">{plugin().description}</p>
            <div class="flex items-center gap-1.5 text-[10px] text-[var(--text-muted)]">
              <span class="px-1.5 py-0.5 rounded-[var(--radius-sm)] border border-[var(--border-subtle)]">
                v{plugin().version}
              </span>
              <span class="px-1.5 py-0.5 rounded-[var(--radius-sm)] border border-[var(--border-subtle)] uppercase">
                {plugin().source}
              </span>
              <span
                class={`px-1.5 py-0.5 rounded-[var(--radius-sm)] border uppercase ${plugin().trust === 'verified' ? 'text-[var(--success)] border-[var(--success)]' : 'text-[var(--accent)] border-[var(--accent)]'}`}
              >
                {plugin().trust}
              </span>
            </div>
            <p class="text-[10px] text-[var(--text-muted)]">{plugin().changelogSummary}</p>
            <p class="text-[10px] text-[var(--text-muted)]">
              Status:{' '}
              <span class="text-[var(--text-secondary)]">
                {props.state?.installed
                  ? props.state.enabled
                    ? 'Installed + enabled'
                    : 'Installed + disabled'
                  : 'Not installed'}
              </span>
            </p>
            <Show when={(props.mounts?.length ?? 0) > 0}>
              <div class="pt-1.5 border-t border-[var(--border-subtle)]">
                <p class="text-[10px] uppercase tracking-wide text-[var(--text-muted)] mb-1.5">
                  Exposed UI Mounts
                </p>
                <div class="space-y-1.5">
                  <For each={props.mounts ?? []}>
                    {(entry) => (
                      <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] px-2 py-1.5">
                        <div class="flex items-center justify-between gap-2">
                          <span class="text-[11px] text-[var(--text-primary)]">
                            {entry.mount.label}
                          </span>
                          <span class="text-[10px] text-[var(--text-muted)] uppercase">
                            {entry.mount.location}
                          </span>
                        </div>
                        <Show when={entry.mount.description}>
                          <p class="text-[10px] text-[var(--text-secondary)] mt-1">
                            {entry.mount.description}
                          </p>
                        </Show>
                        <p class="text-[10px] text-[var(--text-muted)] mt-1">
                          id: {entry.mount.id}
                        </p>
                      </div>
                    )}
                  </For>
                </div>
              </div>
            </Show>
          </div>
        )}
      </Show>
    </div>
  )
}
