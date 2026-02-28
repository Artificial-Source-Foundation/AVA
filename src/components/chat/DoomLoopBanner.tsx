/**
 * Doom Loop Banner
 *
 * Full-width amber warning banner shown when the agent appears stuck
 * in a repetitive loop. Offers Stop, Retry, and Switch Model actions.
 */

import { AlertTriangle, RefreshCw, RotateCcw, Square } from 'lucide-solid'
import type { Component } from 'solid-js'

interface DoomLoopBannerProps {
  onStop: () => void
  onRetry: () => void
  onSwitchModel: () => void
}

export const DoomLoopBanner: Component<DoomLoopBannerProps> = (props) => (
  <div class="flex items-center gap-3 px-3 py-2 rounded-lg bg-[var(--warning-subtle)] border border-[var(--warning)]/30 text-[var(--warning)] text-[13px]">
    <AlertTriangle class="w-4 h-4 flex-shrink-0" />
    <span class="flex-1 font-medium">Agent appears stuck in a loop</span>
    <div class="flex items-center gap-1.5">
      <button
        type="button"
        onClick={props.onStop}
        class="inline-flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--error)] text-white hover:brightness-110 transition-colors"
      >
        <Square class="w-3 h-3" />
        Stop
      </button>
      <button
        type="button"
        onClick={props.onRetry}
        class="inline-flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] hover:bg-[var(--surface-hover)] transition-colors"
      >
        <RotateCcw class="w-3 h-3" />
        Retry
      </button>
      <button
        type="button"
        onClick={props.onSwitchModel}
        class="inline-flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] hover:bg-[var(--surface-hover)] transition-colors"
      >
        <RefreshCw class="w-3 h-3" />
        Switch Model
      </button>
    </div>
  </div>
)
