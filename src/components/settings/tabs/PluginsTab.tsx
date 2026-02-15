import { Puzzle, RefreshCw, Search, Trash2 } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { usePlugins } from '../../../stores/plugins'
import { PluginDetailPanel } from '../../plugins'

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

  const showFeatured = createMemo(
    () =>
      !plugins.search().trim() && plugins.categoryFilter() === 'all' && !plugins.showInstalledOnly()
  )

  const emptyStateMessage = createMemo(() => {
    if (plugins.showInstalledOnly()) return 'No installed plugins match this filter yet.'
    if (plugins.search().trim()) return `No plugins found for "${plugins.search().trim()}".`
    if (plugins.categoryFilter() !== 'all') return 'No plugins in this category.'
    return 'No plugins match your filters.'
  })

  const categoryLabel = (category: string) => category.charAt(0).toUpperCase() + category.slice(1)

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

      <div class="flex items-center gap-1.5 flex-wrap">
        <For each={plugins.categories()}>
          {(category) => (
            <button
              type="button"
              onClick={() => plugins.setCategoryFilter(category)}
              class={`px-2 py-1 text-[10px] rounded-[var(--radius-md)] border ${plugins.categoryFilter() === category ? 'text-[var(--accent)] border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'text-[var(--text-secondary)] border-[var(--border-subtle)] bg-[var(--surface-raised)]'}`}
            >
              {category === 'all' ? 'All' : categoryLabel(category)}
            </button>
          )}
        </For>
      </div>

      <Show when={showFeatured()}>
        <div class="space-y-1.5">
          <p class="text-[10px] uppercase tracking-wide text-[var(--text-muted)]">Featured</p>
          <div class="grid grid-cols-1 sm:grid-cols-2 gap-1.5">
            <For each={plugins.featuredPlugins()}>
              {(plugin) => (
                <button
                  type="button"
                  onClick={() => setSelectedPluginId(plugin.id)}
                  class="text-left px-2.5 py-2 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] hover:border-[var(--accent-muted)] transition-colors"
                >
                  <div class="flex items-center gap-1.5 mb-0.5">
                    <Puzzle class="w-3 h-3 text-[var(--accent)]" />
                    <span class="text-[11px] text-[var(--text-primary)]">{plugin.name}</span>
                  </div>
                  <p class="text-[10px] text-[var(--text-muted)] line-clamp-2">
                    {plugin.description}
                  </p>
                </button>
              )}
            </For>
          </div>
        </div>
      </Show>

      <div class="space-y-1.5">
        <Show
          when={plugins.filteredPlugins().length > 0}
          fallback={<p class="text-[11px] text-[var(--text-muted)]">{emptyStateMessage()}</p>}
        >
          <For each={plugins.filteredPlugins()}>
            {(plugin) => {
              const state = () =>
                plugins.pluginState()[plugin.id] ?? {
                  installed: false,
                  enabled: false,
                }
              const pending = () => plugins.pendingAction(plugin.id)
              const isBusy = () => pending() !== null
              const error = () => plugins.errorFor(plugin.id)
              return (
                <div
                  class={`w-full flex items-center gap-2.5 px-2.5 py-2 rounded-[var(--radius-md)] border ${selectedPluginId() === plugin.id ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'border-[var(--border-subtle)] bg-[var(--surface)]'}`}
                >
                  <div class="flex-1 min-w-0">
                    <button
                      type="button"
                      onClick={() => setSelectedPluginId(plugin.id)}
                      class="w-full flex items-center gap-2.5 min-w-0 text-left"
                    >
                      <Puzzle class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)]" />
                      <div class="flex-1 min-w-0">
                        <p class="text-xs text-[var(--text-primary)]">{plugin.name}</p>
                        <p class="text-[10px] text-[var(--text-muted)]">{plugin.description}</p>
                      </div>
                    </button>
                    <Show when={error()}>
                      <div class="mt-0.5 flex items-center gap-2">
                        <p class="text-[10px] text-[var(--error)]">{error()}</p>
                        <button
                          type="button"
                          onClick={() => {
                            void plugins.retry(plugin.id)
                          }}
                          disabled={isBusy() || plugins.failedAction(plugin.id) === null}
                          class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] border border-[var(--border-default)] text-[var(--text-secondary)] disabled:opacity-50"
                        >
                          Retry
                        </button>
                        <button
                          type="button"
                          onClick={() => {
                            void plugins.recover(plugin.id)
                          }}
                          disabled={isBusy()}
                          class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] border border-[var(--error)] text-[var(--error)] disabled:opacity-50"
                        >
                          Recover
                        </button>
                      </div>
                    </Show>
                  </div>
                  <Show
                    when={state().installed}
                    fallback={
                      <button
                        type="button"
                        onClick={() => {
                          plugins.clearError(plugin.id)
                          void plugins.install(plugin.id)
                        }}
                        disabled={isBusy()}
                        class="px-2 py-1 text-[10px] text-white bg-[var(--accent)] rounded-[var(--radius-md)] disabled:opacity-60"
                      >
                        {pending() === 'install' ? 'Installing...' : 'Install'}
                      </button>
                    }
                  >
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        plugins.clearError(plugin.id)
                        void plugins.toggleEnabled(plugin.id)
                      }}
                      disabled={isBusy()}
                      class={`px-2 py-1 text-[10px] rounded-[var(--radius-md)] border disabled:opacity-60 ${state().enabled ? 'text-[var(--success)] border-[var(--success)]' : 'text-[var(--text-muted)] border-[var(--border-default)]'}`}
                    >
                      {pending() === 'toggle'
                        ? 'Updating...'
                        : state().enabled
                          ? 'Enabled'
                          : 'Disabled'}
                    </button>
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        plugins.clearError(plugin.id)
                        void plugins.uninstall(plugin.id)
                      }}
                      disabled={isBusy()}
                      class="p-1.5 text-[var(--error)] hover:bg-[var(--error-subtle)] rounded-[var(--radius-sm)] disabled:opacity-60"
                      aria-label="Uninstall plugin"
                    >
                      <Show
                        when={pending() === 'uninstall'}
                        fallback={<Trash2 class="w-3.5 h-3.5" />}
                      >
                        <RefreshCw class="w-3.5 h-3.5 animate-spin" />
                      </Show>
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
