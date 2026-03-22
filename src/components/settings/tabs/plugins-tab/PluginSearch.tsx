/**
 * Plugin Search & Filters
 *
 * Search input, category filter pills, sort dropdown, and catalog status bar.
 */

import { ArrowUpDown, Search } from 'lucide-solid'
import { type Component, For } from 'solid-js'
import { usePlugins } from '../../../../stores/plugins'
import type { PluginSortBy } from '../../../../stores/plugins-catalog'
import { categoryLabel } from './plugin-utils'

export interface PluginSearchProps {
  sortBy: () => PluginSortBy
  onSortChange: (value: PluginSortBy) => void
}

export const PluginSearch: Component<PluginSearchProps> = (props) => {
  const plugins = usePlugins()

  return (
    <>
      <div class="flex items-center gap-2">
        <div class="relative flex-1">
          <Search class="w-3.5 h-3.5 absolute left-2 top-1/2 -translate-y-1/2 text-[var(--text-muted)]" />
          <input
            value={plugins.search()}
            onInput={(e) => plugins.setSearch(e.currentTarget.value)}
            placeholder="Search plugins..."
            class="w-full pl-7 pr-2 py-1.5 text-[var(--settings-text-button)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)]"
          />
        </div>
        <button
          type="button"
          onClick={() => plugins.setShowInstalledOnly(!plugins.showInstalledOnly())}
          class={`px-2 py-1.5 text-[var(--settings-text-badge)] rounded-[var(--radius-md)] border ${plugins.showInstalledOnly() ? 'text-[var(--accent)] border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'text-[var(--text-secondary)] border-[var(--border-subtle)] bg-[var(--surface-raised)]'}`}
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
              class={`px-2 py-1 text-[var(--settings-text-badge)] rounded-[var(--radius-md)] border ${plugins.categoryFilter() === category ? 'text-[var(--accent)] border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'text-[var(--text-secondary)] border-[var(--border-subtle)] bg-[var(--surface-raised)]'}`}
            >
              {category === 'all' ? 'All' : categoryLabel(category)}
            </button>
          )}
        </For>
        <div class="ml-auto flex items-center gap-1">
          <ArrowUpDown class="w-3 h-3 text-[var(--text-muted)]" />
          <select
            value={props.sortBy()}
            onChange={(e) => props.onSortChange(e.currentTarget.value as PluginSortBy)}
            class="text-[var(--settings-text-badge)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)] px-1.5 py-1 outline-none"
          >
            <option value="popular">Popular</option>
            <option value="rated">Top Rated</option>
            <option value="recent">Recent</option>
            <option value="name">Name</option>
          </select>
        </div>
      </div>
    </>
  )
}
