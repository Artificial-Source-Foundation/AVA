import { Puzzle, RefreshCw, Search, Trash2 } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { usePlugins } from '../../../stores/plugins'
import { PluginDetailPanel } from '../../plugins/PluginDetailPanel'

export const PluginsTab: Component = () => {
  const plugins = usePlugins()
  const [selectedPluginId, setSelectedPluginId] = createSignal<string | null>(null)

  const selectedPlugin = createMemo(() => {
    const id = selectedPluginId()
    if (!id) return null
    return plugins.plugins.find((plugin) => plugin.id === id) ?? null
  })

  const selectedState = createMemo(() => {
    const id = selectedPluginId()
    if (!id) return null
    return plugins.pluginState()[id] ?? { installed: false, enabled: false }
  })

  return (
    <div class="space-y-3">
      <div class="flex items-center justify-between">
        <div>
          <p class="text-xs text-[var(--text-secondary)]">Plugin manager (Settings-only)</p>
          <p class="text-[10px] text-[var(--text-muted)]">
            Installed: {plugins.installedCount()} / {plugins.plugins.length}
          </p>
        </div>
        <button
          type="button"
          onClick={() => {
            void plugins.refresh()
          }}
          class="flex items-center gap-1.5 px-2 py-1 text-[10px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]"
        >
          <RefreshCw class="w-3 h-3" />
          Refresh
        </button>
      </div>

      <div class="flex items-center gap-2">
        <div class="relative flex-1">
          <Search class="w-3.5 h-3.5 absolute left-2 top-1/2 -translate-y-1/2 text-[var(--text-muted)]" />
          <input
            value={plugins.search()}
            onInput={(e) => plugins.setSearch(e.currentTarget.value)}
            placeholder="Search plugins..."
            class="w-full pl-7 pr-2 py-1.5 text-[11px] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)]"
          />
        </div>
        <button
          type="button"
          onClick={() => plugins.setShowInstalledOnly(!plugins.showInstalledOnly())}
          class={`px-2 py-1.5 text-[10px] rounded-[var(--radius-md)] border ${plugins.showInstalledOnly() ? 'text-[var(--accent)] border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'text-[var(--text-secondary)] border-[var(--border-subtle)] bg-[var(--surface-raised)]'}`}
        >
          Installed only
        </button>
      </div>

      <div class="space-y-1.5">
        <Show
          when={plugins.filteredPlugins().length > 0}
          fallback={
            <p class="text-[11px] text-[var(--text-muted)]">No plugins match your filters.</p>
          }
        >
          <For each={plugins.filteredPlugins()}>
            {(plugin) => {
              const state = plugins.pluginState()[plugin.id] ?? {
                installed: false,
                enabled: false,
              }
              return (
                <div
                  class={`w-full flex items-center gap-2.5 px-2.5 py-2 rounded-[var(--radius-md)] border ${selectedPluginId() === plugin.id ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'border-[var(--border-subtle)] bg-[var(--surface)]'}`}
                >
                  <button
                    type="button"
                    onClick={() => setSelectedPluginId(plugin.id)}
                    class="flex items-center gap-2.5 flex-1 min-w-0 text-left"
                  >
                    <Puzzle class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)]" />
                    <div class="flex-1 min-w-0">
                      <p class="text-xs text-[var(--text-primary)]">{plugin.name}</p>
                      <p class="text-[10px] text-[var(--text-muted)]">{plugin.description}</p>
                    </div>
                  </button>
                  <Show
                    when={state.installed}
                    fallback={
                      <button
                        type="button"
                        onClick={() => plugins.install(plugin.id)}
                        class="px-2 py-1 text-[10px] text-white bg-[var(--accent)] rounded-[var(--radius-md)]"
                      >
                        Install
                      </button>
                    }
                  >
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        plugins.toggleEnabled(plugin.id)
                      }}
                      class={`px-2 py-1 text-[10px] rounded-[var(--radius-md)] border ${state.enabled ? 'text-[var(--success)] border-[var(--success)]' : 'text-[var(--text-muted)] border-[var(--border-default)]'}`}
                    >
                      {state.enabled ? 'Enabled' : 'Disabled'}
                    </button>
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        plugins.uninstall(plugin.id)
                      }}
                      class="p-1.5 text-[var(--error)] hover:bg-[var(--error-subtle)] rounded-[var(--radius-sm)]"
                      aria-label="Uninstall plugin"
                    >
                      <Trash2 class="w-3.5 h-3.5" />
                    </button>
                  </Show>
                </div>
              )
            }}
          </For>
        </Show>
      </div>

      <PluginDetailPanel plugin={selectedPlugin()} state={selectedState()} />
    </div>
  )
}
