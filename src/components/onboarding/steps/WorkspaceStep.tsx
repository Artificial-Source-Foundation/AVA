/**
 * Step 4: Set Up Workspace
 *
 * Three option cards (vertical stack, 8px gap):
 * - Trust Current Folder: green folder-check icon, mono path, description
 * - Import Existing Config: blue upload icon
 * - Start Fresh: purple sparkles icon
 * Selected = accent border. Nav: Back <- | dots | Continue.
 */

import { FolderCheck, Sparkles, Upload } from 'lucide-solid'
import { type Component, For } from 'solid-js'
import { Dynamic } from 'solid-js/web'

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

export type WorkspaceChoice = 'trust' | 'import' | 'fresh'

interface WorkspaceOption {
  id: WorkspaceChoice
  icon: Component<{ class?: string }>
  iconColor: string
  iconBg: string
  title: string
  description: string
  /** If true, show the current path under the title in mono */
  showPath?: boolean
}

const WORKSPACE_OPTIONS: WorkspaceOption[] = [
  {
    id: 'trust',
    icon: FolderCheck,
    iconColor: '#22C55E',
    iconBg: 'rgba(34, 197, 94, 0.15)',
    title: 'Trust Current Folder',
    description: 'Load local config, MCP servers, and rules from this project',
    showPath: true,
  },
  {
    id: 'import',
    icon: Upload,
    iconColor: '#3B82F6',
    iconBg: 'rgba(59, 130, 246, 0.15)',
    title: 'Import Existing Config',
    description: 'Load .ava/ folder from another project',
  },
  {
    id: 'fresh',
    icon: Sparkles,
    iconColor: '#8B5CF6',
    iconBg: 'rgba(139, 92, 246, 0.15)',
    title: 'Start Fresh',
    description: 'Clean slate \u2014 no local config, no inherited rules',
  },
]

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface WorkspaceStepProps {
  selected: WorkspaceChoice
  currentPath: string
  onSelect: (choice: WorkspaceChoice) => void
  onPrev: () => void
  onNext: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const WorkspaceStep: Component<WorkspaceStepProps> = (props) => (
  <div class="flex flex-col items-center w-full max-w-[520px]">
    {/* Header */}
    <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-2">
      Set Up Workspace
    </h2>
    <p class="text-sm text-[var(--text-muted)] mb-8">How should AVA handle this project?</p>

    {/* Option cards - vertical stack, 8px gap */}
    <div class="w-full flex flex-col gap-2 mb-10">
      <For each={WORKSPACE_OPTIONS}>
        {(option) => (
          <button
            type="button"
            onClick={() => props.onSelect(option.id)}
            class="rounded-xl p-4 text-left transition-all flex items-start gap-3"
            style={{
              background: 'var(--surface)',
              border:
                props.selected === option.id
                  ? '1px solid var(--accent)'
                  : '1px solid var(--border-subtle)',
            }}
          >
            {/* Icon - 36px frame */}
            <div
              class="w-9 h-9 rounded-[10px] flex items-center justify-center flex-shrink-0"
              style={{ background: option.iconBg, color: option.iconColor }}
            >
              <Dynamic component={option.icon} class="w-[18px] h-[18px]" />
            </div>

            {/* Text */}
            <div class="flex-1 min-w-0">
              <p class="text-sm font-medium text-[var(--text-primary)]">{option.title}</p>
              {option.showPath && (
                <p
                  class="text-xs mt-0.5"
                  style={{
                    color: 'var(--text-muted)',
                    'font-family': '"JetBrains Mono", monospace',
                    'font-size': '11px',
                  }}
                >
                  {props.currentPath}
                </p>
              )}
              <p class="text-xs text-[var(--text-muted)] mt-0.5">{option.description}</p>
            </div>
          </button>
        )}
      </For>
    </div>

    {/* Navigation: Back <- | (dots in parent) | Continue */}
    <div class="w-full flex items-center justify-between">
      <button
        type="button"
        onClick={() => props.onPrev()}
        class="text-sm text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors flex items-center gap-1"
      >
        <span aria-hidden="true">&larr;</span>
        Back
      </button>
      <button
        type="button"
        onClick={() => props.onNext()}
        class="px-6 py-2 bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white text-sm font-medium rounded-[10px] transition-colors"
      >
        Continue
      </button>
    </div>
  </div>
)
