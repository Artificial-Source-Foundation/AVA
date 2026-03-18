/**
 * Quick Actions Row
 *
 * Three action buttons: New Session, Open Project, Resume Last.
 * Used on the Project Hub landing page.
 */

import { FolderOpen, History, Plus } from 'lucide-solid'
import type { Component } from 'solid-js'

export interface QuickActionsProps {
  onNewSession: () => void
  onOpenProject: () => void
  onResumeLast: () => void
  hasLastSession: boolean
}

export const QuickActions: Component<QuickActionsProps> = (props) => {
  return (
    <div class="flex items-center gap-3">
      {/* New Session — accent purple */}
      <button
        type="button"
        onClick={() => props.onNewSession()}
        class="
          inline-flex items-center gap-2
          rounded-xl px-5 py-2.5
          bg-[var(--accent)] hover:bg-[var(--accent-hover)]
          text-white text-[13px] font-semibold
          transition-colors duration-150
          cursor-pointer
        "
      >
        <Plus class="w-4 h-4" />
        New Session
      </button>

      {/* Open Project — dark card */}
      <button
        type="button"
        onClick={() => props.onOpenProject()}
        class="
          inline-flex items-center gap-2
          rounded-xl px-5 py-2.5
          bg-[var(--surface-raised)] hover:bg-[var(--surface-overlay)]
          text-[var(--gray-9)] text-[13px] font-medium
          border border-[var(--gray-5)]
          transition-colors duration-150
          cursor-pointer
        "
      >
        <FolderOpen class="w-4 h-4" />
        Open Project
      </button>

      {/* Resume Last — dark card */}
      <button
        type="button"
        onClick={() => props.onResumeLast()}
        disabled={!props.hasLastSession}
        class="
          inline-flex items-center gap-2
          rounded-xl px-5 py-2.5
          bg-[var(--surface-raised)] hover:bg-[var(--surface-overlay)]
          text-[var(--gray-9)] text-[13px] font-medium
          border border-[var(--gray-5)]
          transition-colors duration-150
          cursor-pointer
          disabled:opacity-40 disabled:cursor-not-allowed
        "
      >
        <History class="w-4 h-4" />
        Resume Last
      </button>
    </div>
  )
}
