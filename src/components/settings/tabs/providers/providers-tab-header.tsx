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
    <div class="flex items-center justify-between">
      <div class="flex items-center gap-2">
        <h3 class="text-xs font-medium text-[var(--text-primary)]">Providers</h3>
        <Show when={props.connectedCount() > 0}>
          <span class="px-1.5 py-0.5 text-[9px] font-medium text-[var(--success)] bg-[var(--success)]/10 rounded-full">
            {props.connectedCount()} connected
          </span>
        </Show>
      </div>
    </div>
    <div class="relative">
      <Search class="absolute left-2.5 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--text-muted)]" />
      <input
        type="text"
        value={props.searchQuery()}
        onInput={(e) => props.onSearchChange(e.currentTarget.value)}
        placeholder="Search providers..."
        class="w-full pl-8 pr-3 py-1.5 text-xs bg-[var(--input-background)] text-[var(--text-primary)] placeholder:text-[var(--input-placeholder)] border border-[var(--input-border)] rounded-[var(--radius-md)] focus:outline-none focus:border-[var(--input-border-focus)] transition-colors"
      />
    </div>
  </div>
)
