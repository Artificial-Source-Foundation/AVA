/**
 * Tool Preview
 *
 * Shows a one-line preview of the currently-executing tool, e.g.:
 *   "Edit src/foo.ts lines 10-25"
 *   "Running pwd && ls -la"
 *
 * Only visible while streaming with active (running/pending) tool calls.
 */

import { Loader2 } from 'lucide-solid'
import { type Component, createEffect, createSignal, onCleanup, Show } from 'solid-js'
import type { ToolCall } from '../../types'
import { formatElapsed, summarizeAction } from './tool-call-utils'

interface ToolPreviewProps {
  toolCalls?: ToolCall[]
  isStreaming: boolean
}

export const ToolPreview: Component<ToolPreviewProps> = (props) => {
  const [elapsed, setElapsed] = createSignal('')

  const activeCall = (): ToolCall | undefined => {
    if (!props.toolCalls?.length) return undefined
    for (let i = props.toolCalls.length - 1; i >= 0; i--) {
      const tc = props.toolCalls[i]
      if (tc.status === 'running' || tc.status === 'pending') return tc
    }
    return undefined
  }

  const label = (): string => {
    const call = activeCall()
    if (!call) return ''
    return summarizeAction(call.name, call.args)
  }

  createEffect(() => {
    if (!props.isStreaming) {
      setElapsed('')
      return
    }
    const call = activeCall()
    if (!call) {
      setElapsed('')
      return
    }
    setElapsed(formatElapsed(call.startedAt))
    const timer = setInterval(() => {
      const running = activeCall()
      setElapsed(running ? formatElapsed(running.startedAt) : '')
    }, 1000)
    onCleanup(() => clearInterval(timer))
  })

  return (
    <Show when={props.isStreaming && activeCall()}>
      <div class="flex items-center gap-2 py-1 text-xs text-[var(--text-muted)] animate-fade-in">
        <Loader2 class="w-3 h-3 animate-spin text-[var(--accent)] flex-shrink-0" />
        <span class="truncate">{label()}</span>
        <Show when={elapsed()}>
          <span class="tabular-nums whitespace-nowrap text-[11px]">{elapsed()}</span>
        </Show>
      </div>
    </Show>
  )
}
