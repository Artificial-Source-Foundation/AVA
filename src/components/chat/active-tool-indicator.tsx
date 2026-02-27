/**
 * Active Tool Indicator
 *
 * Replaces the generic "Working..." spinner with a contextual display
 * showing the current tool action and live elapsed time.
 *
 * Layout: [spinner] Editing src/chat/ToolCallCard.tsx  3.2s
 */

import { Loader2 } from 'lucide-solid'
import { type Component, createSignal, onCleanup, Show } from 'solid-js'
import type { ToolCall } from '../../types'
import { formatElapsed, summarizeAction } from './tool-call-utils'

interface ActiveToolIndicatorProps {
  toolCalls?: ToolCall[]
  isStreaming: boolean
}

export const ActiveToolIndicator: Component<ActiveToolIndicatorProps> = (props) => {
  const [elapsed, setElapsed] = createSignal('')

  const activeCall = (): ToolCall | undefined => {
    if (!props.toolCalls?.length) return undefined
    // Find last running tool call
    for (let i = props.toolCalls.length - 1; i >= 0; i--) {
      if (props.toolCalls[i].status === 'running') return props.toolCalls[i]
    }
    return undefined
  }

  const label = () => {
    const call = activeCall()
    if (call) return summarizeAction(call.name, call.args)
    return 'Thinking...'
  }

  // Live elapsed timer
  const timer = setInterval(() => {
    const call = activeCall()
    if (call) {
      setElapsed(formatElapsed(call.startedAt))
    } else {
      setElapsed('')
    }
  }, 1000)

  onCleanup(() => clearInterval(timer))

  return (
    <Show when={props.isStreaming}>
      <div class="flex items-center gap-2 mt-2 px-1 border-l-2 border-[var(--accent)] pl-2">
        <Loader2 class="w-3.5 h-3.5 animate-spin text-[var(--accent-text)]" />
        <span class="text-xs text-[var(--text-muted)] truncate">{label()}</span>
        <Show when={elapsed()}>
          <span class="text-[11px] text-[var(--text-muted)] tabular-nums whitespace-nowrap ml-auto">
            {elapsed()}
          </span>
        </Show>
      </div>
    </Show>
  )
}
