import { Search } from 'lucide-solid'
import type { JSX } from 'solid-js'

interface SessionsSearchProps {
  value: string
  onInput: JSX.EventHandler<HTMLInputElement, InputEvent>
}

export function SessionsSearch(props: SessionsSearchProps): JSX.Element {
  return (
    <div class="density-px density-py flex-shrink-0">
      <div class="relative">
        <Search class="absolute left-2 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--text-muted)]" />
        <input
          type="text"
          placeholder="Search sessions..."
          value={props.value}
          onInput={(event) => props.onInput(event)}
          class="w-full pl-7 pr-2 py-1.5 text-xs text-[var(--text-primary)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] placeholder:text-[var(--text-muted)] focus-glow"
        />
      </div>
    </div>
  )
}
