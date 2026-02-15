/**
 * Status Bar
 *
 * Right-side section of the bottom toolbar:
 * streaming stats, agent indicators, queue badge, cancel & send buttons.
 */

import { ArrowUp, Square } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface StatusBarProps {
  isProcessing: Accessor<boolean>
  isStreaming: Accessor<boolean>
  elapsedSeconds: Accessor<number>
  streamingTokenEstimate: Accessor<number>
  activeToolCallCount: Accessor<number>
  agentIsRunning: Accessor<boolean>
  agentCurrentTurn: Accessor<number>
  doomLoopDetected: Accessor<boolean>
  queuedCount: Accessor<number>
  onCancel: () => void
  inputHasText: Accessor<boolean>
  useAgentMode: Accessor<boolean>
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const StatusBar: Component<StatusBarProps> = (props) => (
  <div class="flex items-center density-gap">
    {/* Streaming stats */}
    <Show when={props.isStreaming()}>
      <span class="flex items-center gap-1.5 text-[10px] text-[var(--text-tertiary)] tabular-nums">
        <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
        {props.elapsedSeconds()}s
        <Show when={props.streamingTokenEstimate() > 0}>
          <span class="text-[var(--border-muted)]">&middot;</span>~
          {props.streamingTokenEstimate().toLocaleString()} tokens
        </Show>
        <Show when={props.activeToolCallCount() > 0}>
          <span class="text-[var(--border-muted)]">&middot;</span>
          {props.activeToolCallCount()} tools
        </Show>
      </span>
    </Show>

    {/* Agent status indicators */}
    <Show when={props.agentIsRunning()}>
      <span class="flex items-center gap-1 text-[10px] text-[var(--text-tertiary)]">
        <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
        Turn {props.agentCurrentTurn()}
      </span>
    </Show>
    <Show when={props.doomLoopDetected()}>
      <span class="text-[10px] text-[var(--warning)]">Loop</span>
    </Show>

    {/* Queue badge */}
    <Show when={props.queuedCount() > 0}>
      <span class="text-[10px] text-[var(--accent)] font-medium tabular-nums">
        {props.queuedCount()} queued
      </span>
    </Show>

    {/* Cancel button */}
    <Show when={props.isProcessing()}>
      <button
        type="button"
        onClick={props.onCancel}
        class="
          p-2
          bg-[var(--error)] hover:brightness-110
          text-white
          rounded-[var(--radius-md)]
          transition-colors
        "
      >
        <Square class="w-4 h-4" />
      </button>
    </Show>

    {/* Send / Queue button */}
    <button
      type="submit"
      disabled={!props.inputHasText() || (props.useAgentMode() && props.isProcessing())}
      class={`
        p-2 rounded-[var(--radius-md)] transition-colors
        disabled:opacity-30 disabled:cursor-not-allowed
        ${
          !props.useAgentMode() && props.isProcessing()
            ? 'bg-[var(--surface-raised)] border border-[var(--accent-border)] text-[var(--accent)] hover:bg-[var(--accent-subtle)]'
            : 'bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white'
        }
      `}
      title={
        !props.useAgentMode() && props.isProcessing()
          ? 'Queue message (Ctrl+Shift+Enter to steer)'
          : 'Send message'
      }
    >
      <ArrowUp class="w-4 h-4" />
    </button>
  </div>
)
