/**
 * Sessions Toolbar
 *
 * Header row for the session sidebar with:
 * - "Sessions" title
 * - Project dropdown selector
 * - Git branch / list view toggle
 * - Purple "+" new session button (accent colored)
 */

import { GitBranch, List, Plus } from 'lucide-solid'
import { type JSX, Show } from 'solid-js'
import { ProjectDropdown } from './ProjectDropdown'

interface SessionsToolbarProps {
  viewMode: 'list' | 'tree'
  onToggleView: () => void
  onNewChat: () => void
}

const iconBtnClass =
  'flex items-center justify-center w-6 h-6 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors'

export function SessionsToolbar(props: SessionsToolbarProps): JSX.Element {
  return (
    <div class="flex items-center justify-between px-4 py-3.5 flex-shrink-0">
      <span class="text-[13px] font-semibold text-[var(--gray-9)]">Sessions</span>

      <div class="flex items-center gap-1.5">
        <ProjectDropdown />

        <button
          type="button"
          onClick={() => props.onToggleView()}
          class={iconBtnClass}
          title={props.viewMode === 'list' ? 'Tree view' : 'List view'}
          aria-label={props.viewMode === 'list' ? 'Switch to tree view' : 'Switch to list view'}
        >
          <Show when={props.viewMode === 'list'} fallback={<List class="w-4 h-4" />}>
            <GitBranch class="w-4 h-4" />
          </Show>
        </button>

        {/* New session — purple accent button */}
        <button
          type="button"
          onClick={() => props.onNewChat()}
          class="
            flex items-center justify-center
            w-[26px] h-[26px]
            rounded-[var(--radius-lg)]
            bg-[var(--violet-8)] hover:bg-[var(--accent)]
            text-white
            transition-colors
          "
          title="New chat (Ctrl+N)"
          aria-label="New chat"
        >
          <Plus class="w-3.5 h-3.5" />
        </button>
      </div>
    </div>
  )
}
