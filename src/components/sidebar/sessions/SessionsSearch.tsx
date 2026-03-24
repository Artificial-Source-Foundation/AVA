/**
 * Sessions Search
 *
 * Search input for filtering sessions in the sidebar.
 * Rounded with search icon, matching Soft Zinc design.
 */

import { Search } from 'lucide-solid'
import type { JSX } from 'solid-js'

interface SessionsSearchProps {
  value: string
  onInput: JSX.EventHandler<HTMLInputElement, InputEvent>
}

export function SessionsSearch(props: SessionsSearchProps): JSX.Element {
  return (
    <div class="px-3.5 pb-2 flex-shrink-0">
      <div class="relative">
        <Search class="absolute left-3 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--gray-7)]" />
        <input
          type="text"
          placeholder="Search sessions..."
          value={props.value}
          onInput={(event) => props.onInput(event)}
          class="
            w-full pl-8 pr-3 py-2
            text-[var(--text-base)] text-[var(--text-primary)]
            bg-[var(--gray-3)]
            border-none
            rounded-[var(--radius-xl)]
            placeholder:text-[var(--gray-7)]
            focus:outline-none focus:ring-1 focus:ring-[var(--accent)]
            transition-colors
          "
        />
      </div>
    </div>
  )
}
