import { GitBranch, List, Plus } from 'lucide-solid'
import { type JSX, Show } from 'solid-js'
import { ProjectDropdown } from './ProjectDropdown'

interface SessionsToolbarProps {
  viewMode: 'list' | 'tree'
  onToggleView: () => void
  onNewChat: () => void
}

const buttonClass =
  'flex items-center justify-center w-6 h-6 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors'

export function SessionsToolbar(props: SessionsToolbarProps): JSX.Element {
  return (
    <div class="flex items-center justify-between density-px h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
      <span class="text-xs font-semibold text-[var(--text-secondary)] uppercase tracking-wider">
        Sessions
      </span>

      <div class="flex items-center gap-1">
        <ProjectDropdown />

        <button
          type="button"
          onClick={() => props.onToggleView()}
          class={buttonClass}
          title={props.viewMode === 'list' ? 'Tree view' : 'List view'}
          aria-label={props.viewMode === 'list' ? 'Switch to tree view' : 'Switch to list view'}
        >
          <Show when={props.viewMode === 'list'} fallback={<List class="w-4 h-4" />}>
            <GitBranch class="w-4 h-4" />
          </Show>
        </button>

        <button
          type="button"
          onClick={() => props.onNewChat()}
          class={buttonClass}
          title="New chat (Ctrl+N)"
          aria-label="New chat"
        >
          <Plus class="w-4 h-4" />
        </button>
      </div>
    </div>
  )
}
