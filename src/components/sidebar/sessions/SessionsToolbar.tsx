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
      <span class="text-[var(--text-base)] font-semibold text-[var(--gray-9)]">Sessions</span>

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

        {/* New chat — primary CTA pill with label */}
        <button
          type="button"
          onClick={() => props.onNewChat()}
          class="
            inline-flex items-center justify-center gap-1
            h-[26px] px-3
            rounded-full
            bg-[var(--accent)] hover:opacity-90
            text-white font-medium
            transition-opacity
          "
          title="New chat (Ctrl+N)"
          aria-label="New chat"
        >
          <Plus class="w-3 h-3 flex-shrink-0" />
          <span class="text-[var(--text-sm)] leading-none">New</span>
        </button>
      </div>
    </div>
  )
}
