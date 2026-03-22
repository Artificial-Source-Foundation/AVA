/**
 * Providers Tab Header
 *
 * Connected count badge and search input.
 */

import { Search } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'

interface ProvidersTabHeaderProps {
  connectedCount: Accessor<number>
  searchQuery: Accessor<string>
  onSearchChange: (value: string) => void
}

export const ProvidersTabHeader: Component<ProvidersTabHeaderProps> = (props) => (
  <div class="space-y-3">
    <Show when={props.connectedCount() > 0}>
      <div class="flex items-center gap-2">
        <span class="px-2 py-0.5 text-[var(--settings-text-badge)] rounded-full bg-[var(--success)]/10 text-[var(--success)] border border-[var(--success)]/20">
          {props.connectedCount()} connected
        </span>
      </div>
    </Show>
    <div class="relative">
      <Search class="absolute left-2.5 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--text-muted)]" />
      <input
        type="text"
        value={props.searchQuery()}
        onInput={(e) => props.onSearchChange(e.currentTarget.value)}
        placeholder="Search providers..."
        class="w-full pl-8 pr-3 py-2 text-xs bg-[var(--input-background)] text-[var(--text-primary)] placeholder:text-[var(--input-placeholder)] border border-[var(--input-border)] rounded-[var(--radius-md)] focus:outline-none focus:border-[var(--input-border-focus)] transition-colors"
      />
    </div>
  </div>
)
