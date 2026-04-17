/**
 * Providers Tab Header
 *
 * Search input for the providers list.
 */

import { Search } from 'lucide-solid'
import type { Accessor, Component } from 'solid-js'

interface ProvidersTabHeaderProps {
  searchQuery: Accessor<string>
  onSearchChange: (value: string) => void
}

export const ProvidersTabHeader: Component<ProvidersTabHeaderProps> = (props) => (
  <div>
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
