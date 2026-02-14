import { Download, Search, Settings, Sparkles, Trash2 } from 'lucide-solid'
import { type Component, For, onMount, Show } from 'solid-js'
import { usePlugins } from '../../../stores/plugins'
import type { PluginManifest } from '../../../types'

export const PluginsTab: Component = () => {
  const {
    filteredPlugins,
    featuredPlugins,
    categories,
    isCatalogLoading,
    searchQuery,
    activeCategory,
    settingsTargetPluginId,
    loadCatalog,
    setSearchQuery,
    setActiveCategory,
    isInstalled,
    isPending,
    installPlugin,
    uninstallPlugin,
    openPluginSettings,
    clearPluginSettingsTarget,
  } = usePlugins()

  onMount(() => {
    void loadCatalog()
  })

  return (
    <div class="space-y-4">
      <div class="rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] p-3">
        <div class="flex flex-wrap items-center justify-between gap-2">
          <h3 class="text-xs font-semibold text-[var(--text-primary)]">Plugin Marketplace</h3>
          <div class="relative w-full sm:w-64">
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

        <div class="mt-3 flex flex-wrap gap-1.5">
          <button
            type="button"
            onClick={() => setActiveCategory('all')}
            class={`rounded-[var(--radius-md)] px-2 py-1 text-[10px] font-medium transition-colors ${
              activeCategory() === 'all'
                ? 'bg-[var(--accent)] text-white'
                : 'bg-[var(--alpha-white-5)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
            }`}
          >
            All
          </button>
          <For each={categories()}>
            {(category) => (
              <button
                type="button"
                onClick={() => setActiveCategory(category)}
                class={`rounded-[var(--radius-md)] px-2 py-1 text-[10px] font-medium capitalize transition-colors ${
                  activeCategory() === category
                    ? 'bg-[var(--accent)] text-white'
                    : 'bg-[var(--alpha-white-5)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
                }`}
              >
                {category}
              </button>
            )}
          </For>
        </div>
      </div>

      <Show when={settingsTargetPluginId()}>
        <div class="rounded-[var(--radius-md)] border border-[var(--accent-muted)] bg-[var(--accent-subtle)] px-3 py-2 text-xs text-[var(--text-primary)]">
          Opening settings for <span class="font-semibold">{settingsTargetPluginId()}</span>
          <button
            type="button"
            onClick={clearPluginSettingsTarget}
            class="ml-2 text-[var(--accent)] hover:text-[var(--accent-hover)]"
          >
            clear
          </button>
        </div>
      </Show>

      <Show
        when={featuredPlugins().length > 0 && activeCategory() === 'all' && !searchQuery().trim()}
      >
        <section>
          <div class="mb-2 flex items-center gap-1.5 text-[10px] font-semibold uppercase tracking-wider text-[var(--text-muted)]">
            <Sparkles class="h-3 w-3" />
            Featured
          </div>
          <div class="grid gap-2">
            <For each={featuredPlugins()}>{(plugin) => <PluginCard plugin={plugin} />}</For>
          </div>
        </section>
      </Show>

      <section>
        <div class="mb-2 text-[10px] font-semibold uppercase tracking-wider text-[var(--text-muted)]">
          All plugins
        </div>
        <Show
          when={!isCatalogLoading()}
          fallback={<p class="text-xs text-[var(--text-muted)]">Loading plugin catalog...</p>}
        >
          <Show
            when={filteredPlugins().length > 0}
            fallback={<p class="text-xs text-[var(--text-muted)]">No plugins match this filter.</p>}
          >
            <div class="grid gap-2">
              <For each={filteredPlugins()}>
                {(plugin) => (
                  <PluginCard
                    plugin={plugin}
                    installed={isInstalled(plugin.id)}
                    pending={isPending(plugin.id)}
                    onInstall={() => void installPlugin(plugin.id)}
                    onUninstall={() => void uninstallPlugin(plugin.id)}
                    onOpenSettings={() => openPluginSettings(plugin.id)}
                  />
                )}
              </For>
            </div>
          </Show>
        </Show>
      </section>
    </div>
  )
}

interface PluginCardProps {
  plugin: PluginManifest
  installed?: boolean
  pending?: boolean
  onInstall?: () => void
  onUninstall?: () => void
  onOpenSettings?: () => void
}

const PluginCard: Component<PluginCardProps> = (props) => {
  const installed = () => props.installed ?? false
  const pending = () => props.pending ?? false

  return (
    <div class="rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2.5">
      <div class="flex items-start justify-between gap-2">
        <div class="min-w-0">
          <div class="flex items-center gap-1.5">
            <span class="truncate text-xs font-semibold text-[var(--text-primary)]">
              {props.plugin.name}
            </span>
            <span class="rounded-[var(--radius-sm)] bg-[var(--alpha-white-8)] px-1.5 py-0.5 text-[9px] text-[var(--text-muted)]">
              {props.plugin.version}
            </span>
          </div>
          <p class="mt-0.5 text-[11px] text-[var(--text-secondary)]">{props.plugin.description}</p>
          <p class="mt-1 text-[10px] capitalize text-[var(--text-muted)]">
            {props.plugin.category} • {props.plugin.author}
          </p>
        </div>

        <div class="flex items-center gap-1.5">
          <Show
            when={installed()}
            fallback={
              <button
                type="button"
                onClick={props.onInstall}
                disabled={pending()}
                class="inline-flex items-center gap-1 rounded-[var(--radius-md)] bg-[var(--accent)] px-2 py-1 text-[10px] font-medium text-white hover:bg-[var(--accent-hover)] disabled:opacity-60"
              >
                <Download class="h-3 w-3" />
                Install
              </button>
            }
          >
            <button
              type="button"
              onClick={props.onUninstall}
              disabled={pending()}
              class="inline-flex items-center gap-1 rounded-[var(--radius-md)] border border-[var(--border-subtle)] px-2 py-1 text-[10px] font-medium text-[var(--text-secondary)] hover:text-[var(--text-primary)] disabled:opacity-60"
            >
              <Trash2 class="h-3 w-3" />
              Uninstall
            </button>
          </Show>

          <Show when={installed() && props.plugin.hasSettings}>
            <button
              type="button"
              onClick={props.onOpenSettings}
              class="inline-flex items-center gap-1 rounded-[var(--radius-md)] border border-[var(--border-subtle)] px-2 py-1 text-[10px] font-medium text-[var(--text-secondary)] hover:text-[var(--text-primary)]"
            >
              <Settings class="h-3 w-3" />
              Settings
            </button>
          </Show>
        </div>
      </div>
    </div>
  )
}
