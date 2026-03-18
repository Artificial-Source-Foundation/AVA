/**
 * Step 4: Set Up Your Workspace
 *
 * Three radio-style option cards: Trust Current Folder, Import Existing Config, Start Fresh.
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
}

const WORKSPACE_OPTIONS: WorkspaceOption[] = [
  {
    id: 'trust',
    icon: FolderCheck,
    iconColor: '#22C55E',
    iconBg: 'rgba(34, 197, 94, 0.15)',
    title: 'Trust Current Folder',
    description: '',
  },
  {
    id: 'import',
    icon: Upload,
    iconColor: '#A78BFA',
    iconBg: 'rgba(139, 92, 246, 0.15)',
    title: 'Import Existing Config',
    description: 'Load .ava/ folder from another project',
  },
  {
    id: 'fresh',
    icon: Sparkles,
    iconColor: '#F59E0B',
    iconBg: 'rgba(245, 158, 11, 0.15)',
    title: 'Start Fresh',
    description: 'Clean slate \u2014 no imported rules or tools',
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
  <div class="flex flex-col items-center">
    {/* Header */}
    <h2 class="text-2xl font-bold text-[#FAFAFA] tracking-tight mb-2">Set Up Your Workspace</h2>
    <p class="text-sm text-[#71717A] mb-8">Choose how to handle your project folder</p>

    {/* Option cards */}
    <div class="w-full max-w-[520px] flex flex-col gap-3 mb-10">
      <For each={WORKSPACE_OPTIONS}>
        {(option) => (
          <button
            type="button"
            onClick={() => props.onSelect(option.id)}
            class="bg-[#18181B] border rounded-xl p-4 text-left transition-all hover:border-[#3F3F46] flex items-start gap-3"
            classList={{
              'border-[#A78BFA]': props.selected === option.id,
              'border-[#27272A]': props.selected !== option.id,
            }}
          >
            {/* Icon */}
            <div
              class="w-10 h-10 rounded-[10px] flex items-center justify-center flex-shrink-0"
              style={{ background: option.iconBg, color: option.iconColor }}
            >
              <Dynamic component={option.icon} class="w-5 h-5" />
            </div>

            {/* Text */}
            <div class="flex-1 min-w-0">
              <p class="text-sm font-medium text-[#FAFAFA]">{option.title}</p>
              <p class="text-xs text-[#71717A] mt-0.5">
                {option.id === 'trust' ? props.currentPath : option.description}
              </p>
            </div>
          </button>
        )}
      </For>
    </div>

    {/* Navigation */}
    <div class="w-full max-w-[520px] flex items-center justify-between">
      <button
        type="button"
        onClick={props.onPrev}
        class="text-sm text-[#71717A] hover:text-[#FAFAFA] transition-colors"
      >
        Back
      </button>
      <button
        type="button"
        onClick={props.onNext}
        class="px-6 py-2.5 bg-[#A78BFA] hover:bg-[#8B5CF6] text-white text-sm font-medium rounded-xl transition-colors"
      >
        Continue
      </button>
    </div>
  </div>
)
