import { type Component, Show } from 'solid-js'
import type { PluginCatalogItem, PluginState } from '../../types/plugin'

interface PluginDetailPanelProps {
  plugin: PluginCatalogItem | null
  state: PluginState | null
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
          </div>
        )}
      </Show>
    </div>
  )
}
