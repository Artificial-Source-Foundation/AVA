import { Download, Search, Settings, Trash2 } from 'lucide-solid'
import { type Component, For, onMount, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { usePlugins } from '../../stores/plugins'

export const SidebarPlugins: Component = () => {
  const {
    filteredPlugins,
    isCatalogLoading,
    searchQuery,
    loadCatalog,
    setSearchQuery,
    isInstalled,
    isPending,
    installPlugin,
    uninstallPlugin,
    openPluginSettings,
  } = usePlugins()
  const { openSettings } = useLayout()

  onMount(() => {
    void loadCatalog()
  })

  return (
    <div class="flex h-full flex-col overflow-hidden">
      <div class="density-px density-py flex-shrink-0 border-b border-[var(--border-subtle)]">
        <h2 class="text-[10px] font-semibold uppercase tracking-wider text-[var(--text-muted)]">
          Plugins
        </h2>
      </div>

      <div class="density-px density-py flex-shrink-0 border-b border-[var(--border-subtle)]">
        <div class="relative">
          <Search class="pointer-events-none absolute left-2 top-1/2 h-3.5 w-3.5 -translate-y-1/2 text-[var(--text-muted)]" />
          <input
            type="text"
            value={searchQuery()}
            onInput={(e) => setSearchQuery(e.currentTarget.value)}
            placeholder="Search plugins"
            class="w-full rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--input-background)] py-1.5 pl-7 pr-2 text-xs text-[var(--text-primary)] focus-glow"
          />
        </div>
      </div>

      <div class="flex-1 overflow-y-auto density-py">
        <Show
          when={!isCatalogLoading()}
          fallback={<p class="density-px text-xs text-[var(--text-muted)]">Loading plugins...</p>}
        >
          <Show
            when={filteredPlugins().length > 0}
            fallback={<p class="density-px text-xs text-[var(--text-muted)]">No plugins found.</p>}
          >
            <div class="space-y-1.5 px-2 pb-2">
              <For each={filteredPlugins()}>
                {(plugin) => (
                  <div class="rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] p-2">
                    <p class="truncate text-xs font-medium text-[var(--text-primary)]">
                      {plugin.name}
                    </p>
                    <p class="mt-0.5 line-clamp-2 text-[10px] text-[var(--text-muted)]">
                      {plugin.description}
                    </p>

                    <div class="mt-2 flex items-center gap-1">
                      <Show
                        when={isInstalled(plugin.id)}
                        fallback={
                          <button
                            type="button"
                            onClick={() => void installPlugin(plugin.id)}
                            disabled={isPending(plugin.id)}
                            class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] bg-[var(--accent)] px-1.5 py-1 text-[10px] text-white disabled:opacity-60"
                          >
                            <Download class="h-3 w-3" />
                            Install
                          </button>
                        }
                      >
                        <button
                          type="button"
                          onClick={() => void uninstallPlugin(plugin.id)}
                          disabled={isPending(plugin.id)}
                          class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--border-subtle)] px-1.5 py-1 text-[10px] text-[var(--text-secondary)] disabled:opacity-60"
                        >
                          <Trash2 class="h-3 w-3" />
                          Uninstall
                        </button>
                      </Show>

                      <Show when={isInstalled(plugin.id) && plugin.hasSettings}>
                        <button
                          type="button"
                          onClick={() => {
                            openPluginSettings(plugin.id)
                            openSettings()
                          }}
                          class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--border-subtle)] px-1.5 py-1 text-[10px] text-[var(--text-secondary)]"
                        >
                          <Settings class="h-3 w-3" />
                          Settings
                        </button>
                      </Show>
                    </div>
                  </div>
                )}
              </For>
            </div>
          </Show>
        </Show>
      </div>
    </div>
  )
}
