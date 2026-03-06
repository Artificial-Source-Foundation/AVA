/**
 * Archived Sessions Section
 *
 * Collapsible section at the bottom of the session sidebar for archived sessions.
 */

import { Archive, ArchiveRestore, ChevronRight } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { SessionWithStats } from '../../../types'
import { formatSessionName } from './session-utils'

export interface ArchivedSectionProps {
  archivedSessions: () => SessionWithStats[]
  loadArchived: () => Promise<void>
  unarchive: (id: string) => void
}

export const ArchivedSection: Component<ArchivedSectionProps> = (props) => {
  const [showArchived, setShowArchived] = createSignal(false)

  return (
    <div class="flex-shrink-0 border-t border-[var(--border-subtle)]">
      <button
        type="button"
        onClick={() => {
          const next = !showArchived()
          setShowArchived(next)
          if (next) void props.loadArchived()
        }}
        class="w-full flex items-center gap-1.5 px-3 py-2 text-[10px] font-semibold tracking-wider text-[var(--text-muted)] uppercase hover:text-[var(--text-secondary)] transition-colors"
      >
        <ChevronRight class={`w-3 h-3 transition-transform ${showArchived() ? 'rotate-90' : ''}`} />
        <Archive class="w-3 h-3" />
        <span>Archived</span>
        <Show when={props.archivedSessions().length > 0}>
          <span class="text-[9px] ml-auto opacity-60">{props.archivedSessions().length}</span>
        </Show>
      </button>
      <Show when={showArchived()}>
        <div class="px-1.5 pb-2 max-h-[200px] overflow-y-auto scrollbar-none">
          <For each={props.archivedSessions()}>
            {(session) => (
              <div class="flex items-center gap-2 density-px density-py rounded-[var(--radius-md)] text-[var(--text-muted)] hover:bg-[var(--sidebar-item-hover)] hover:text-[var(--text-secondary)] transition-colors">
                <Archive class="w-3 h-3 flex-shrink-0 opacity-50" />
                <span class="flex-1 text-xs truncate">{formatSessionName(session.name)}</span>
                <button
                  type="button"
                  onClick={() => void props.unarchive(session.id)}
                  class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]"
                  title="Unarchive"
                  aria-label="Unarchive session"
                >
                  <ArchiveRestore class="w-3 h-3" />
                </button>
              </div>
            )}
          </For>
          <Show when={props.archivedSessions().length === 0}>
            <p class="text-[10px] text-[var(--text-muted)] px-2 py-1 text-center">
              No archived sessions
            </p>
          </Show>
        </div>
      </Show>
    </div>
  )
}
