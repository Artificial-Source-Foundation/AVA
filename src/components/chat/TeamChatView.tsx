/**
 * Team Chat View
 *
 * Full-screen replacement for the main chat when viewing a team member's
 * conversation. Shows breadcrumb navigation, message history, tool call
 * timeline, status, stop button, and input for follow-up messages.
 */

import { CheckCircle, Loader2, Octagon, Users, XCircle } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, onCleanup, Show } from 'solid-js'
import { useTeam } from '../../stores/team'
import type { ToolCall } from '../../types'
import {
  TEAM_DOMAINS,
  type TeamMember,
  type TeamMessage,
  type TeamToolCall,
} from '../../types/team'
import { TeamChatBreadcrumb } from './TeamChatBreadcrumb'
import { TeamChatInput } from './TeamChatInput'
import { ToolCallCard } from './ToolCallCard'
import { formatElapsed } from './tool-call-utils'

interface TeamChatViewProps {
  onStopAgent: (memberId: string) => void
  onSendMessage: (memberId: string, message: string) => void
}

/** Convert TeamToolCall to the ToolCall shape that ToolCallCard expects. */
function asToolCall(tc: TeamToolCall): ToolCall {
  return {
    id: tc.id,
    name: tc.name,
    args: tc.args ?? {},
    status: tc.status === 'running' ? 'running' : tc.status,
    output: tc.output,
    error: tc.error,
    startedAt: tc.startedAt,
    completedAt: tc.completedAt,
  }
}

export const TeamChatView: Component<TeamChatViewProps> = (props) => {
  const team = useTeam()
  const [elapsed, setElapsed] = createSignal('')

  const member = (): TeamMember | null => team.selectedMember()
  const isWorking = () => member()?.status === 'working'
  const isDone = () => member()?.status === 'done'
  const isError = () => member()?.status === 'error'

  const domainConfig = createMemo(() => {
    const m = member()
    return m ? TEAM_DOMAINS[m.domain] : TEAM_DOMAINS.general
  })

  /** Children of the selected member (for leads). */
  const children = createMemo((): TeamMember[] => {
    const m = member()
    return m ? team.getChildren(m.id) : []
  })

  /** Interleaved timeline: messages + tool calls, sorted by timestamp. */
  const timeline = createMemo(() => {
    const m = member()
    if (!m) return []

    type TimelineItem =
      | { kind: 'message'; data: TeamMessage }
      | { kind: 'tool'; data: TeamToolCall }

    const items: TimelineItem[] = [
      ...m.messages.map((msg) => ({ kind: 'message' as const, data: msg })),
      ...m.toolCalls.map((tc) => ({ kind: 'tool' as const, data: tc })),
    ]

    items.sort((a, b) => {
      const tsA = a.kind === 'message' ? a.data.timestamp : a.data.startedAt
      const tsB = b.kind === 'message' ? b.data.timestamp : b.data.startedAt
      return tsA - tsB
    })
    return items
  })

  // Live elapsed timer
  const timer = setInterval(() => {
    const m = member()
    if (m && isWorking()) {
      setElapsed(formatElapsed(m.createdAt))
    }
  }, 1000)

  onCleanup(() => clearInterval(timer))

  return (
    <div class="flex flex-col h-full min-h-0 bg-[var(--surface)]">
      {/* Breadcrumb navigation */}
      <TeamChatBreadcrumb />

      {/* Header: member info + status + stop button */}
      <Show when={member()}>
        {(m) => (
          <div class="flex items-center gap-3 px-4 py-3 border-b border-[var(--border-subtle)]">
            {/* Domain badge */}
            <div
              class="flex items-center justify-center w-8 h-8 rounded-full text-[14px] font-semibold"
              style={{ background: domainConfig().colorSubtle, color: domainConfig().color }}
            >
              {domainConfig().short}
            </div>

            {/* Name + role */}
            <div class="flex-1 min-w-0">
              <div class="text-[14px] font-medium text-[var(--text-primary)] truncate">
                {m().name}
              </div>
              <div class="text-[11px] text-[var(--text-muted)] truncate">
                {m().task ?? 'No task assigned'}
              </div>
            </div>

            {/* Status badge */}
            <Show when={isWorking()}>
              <span class="flex items-center gap-1.5 text-[11px] px-2 py-1 rounded-full bg-[var(--accent)]/10 text-[var(--accent-text)]">
                <Loader2 class="w-3 h-3 animate-spin" />
                Working {elapsed() && <span class="tabular-nums">{elapsed()}</span>}
              </span>
            </Show>
            <Show when={isDone()}>
              <span class="flex items-center gap-1.5 text-[11px] px-2 py-1 rounded-full bg-[var(--success)]/10 text-[var(--success)]">
                <CheckCircle class="w-3 h-3" />
                Done
              </span>
            </Show>
            <Show when={isError()}>
              <span class="flex items-center gap-1.5 text-[11px] px-2 py-1 rounded-full bg-[var(--error)]/10 text-[var(--error)]">
                <XCircle class="w-3 h-3" />
                Error
              </span>
            </Show>

            {/* Stop button */}
            <Show when={isWorking()}>
              <button
                type="button"
                onClick={() => props.onStopAgent(m().id)}
                class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-[var(--radius-md)] text-[12px] text-[var(--error)] bg-[var(--error)]/10 hover:bg-[var(--error)]/20 transition-colors duration-[var(--duration-fast)]"
                title="Stop this agent"
              >
                <Octagon class="w-3.5 h-3.5" />
                Stop
              </button>
            </Show>
          </div>
        )}
      </Show>

      {/* Children (sub-delegations) */}
      <Show when={children().length > 0}>
        <div class="px-4 py-2 border-b border-[var(--border-subtle)] bg-[var(--bg-subtle)]/50">
          <div class="text-[11px] text-[var(--text-muted)] mb-1.5">Sub-delegations</div>
          <div class="flex flex-wrap gap-1.5">
            <For each={children()}>
              {(child) => (
                <button
                  type="button"
                  onClick={() => team.setSelectedMemberId(child.id)}
                  class="flex items-center gap-1.5 px-2 py-1 rounded-full text-[11px] border border-[var(--border-subtle)] hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)] cursor-pointer"
                >
                  <span
                    class="w-1.5 h-1.5 rounded-full"
                    classList={{
                      'bg-[var(--accent)] animate-pulse-subtle': child.status === 'working',
                      'bg-[var(--success)]': child.status === 'done',
                      'bg-[var(--error)]': child.status === 'error',
                      'bg-[var(--text-muted)]': child.status === 'idle',
                    }}
                  />
                  <span class="text-[var(--text-secondary)]">{child.name}</span>
                </button>
              )}
            </For>
          </div>
        </div>
      </Show>

      {/* Timeline: messages + tool calls */}
      <div class="flex-1 overflow-y-auto scrollbar-thin px-4 py-3 space-y-2">
        <Show
          when={timeline().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-[var(--text-muted)] text-[13px]">
              <Users class="w-8 h-8 mb-2 opacity-40" />
              <span>Waiting for activity...</span>
            </div>
          }
        >
          <For each={timeline()}>
            {(item) => (
              <Show
                when={item.kind === 'message'}
                fallback={<ToolCallCard toolCall={asToolCall(item.data as TeamToolCall)} />}
              >
                <MessageItem data={item.data as TeamMessage} />
              </Show>
            )}
          </For>
        </Show>

        {/* Result/Error at the bottom */}
        <Show when={member()?.result}>
          <div class="mt-3 p-3 rounded-[var(--radius-md)] bg-[var(--success)]/5 border border-[var(--success)]/20">
            <div class="text-[11px] text-[var(--success)] font-medium mb-1">Result</div>
            <pre class="text-[12px] text-[var(--text-secondary)] whitespace-pre-wrap break-words font-mono leading-relaxed max-h-48 overflow-y-auto scrollbar-none">
              {member()!.result}
            </pre>
          </div>
        </Show>
        <Show when={member()?.error}>
          <div class="mt-3 p-3 rounded-[var(--radius-md)] bg-[var(--error)]/5 border border-[var(--error)]/20">
            <div class="text-[11px] text-[var(--error)] font-medium mb-1">Error</div>
            <pre class="text-[12px] text-[var(--text-secondary)] whitespace-pre-wrap break-words font-mono leading-relaxed">
              {member()!.error}
            </pre>
          </div>
        </Show>
      </div>

      {/* Input bar */}
      <TeamChatInput onSendMessage={props.onSendMessage} />
    </div>
  )
}

// --- Timeline Sub-Components ---

const MessageItem: Component<{ data: TeamMessage }> = (props) => {
  const isAssistant = () => props.data.role === 'assistant'
  return (
    <div
      class="rounded-[var(--radius-md)] px-3 py-2 text-[13px] leading-relaxed"
      classList={{
        'bg-[var(--alpha-white-3)] text-[var(--text-secondary)]': isAssistant(),
        'bg-[var(--accent)]/10 text-[var(--text-primary)]': !isAssistant(),
      }}
    >
      <div class="text-[10px] text-[var(--text-muted)] mb-0.5">
        {isAssistant() ? 'thinking' : 'user'}
        <span class="ml-2 tabular-nums">
          {new Date(props.data.timestamp).toLocaleTimeString([], {
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit',
          })}
        </span>
      </div>
      <div class="whitespace-pre-wrap break-words">{props.data.content}</div>
    </div>
  )
}
