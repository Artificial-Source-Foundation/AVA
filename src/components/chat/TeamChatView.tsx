/**
 * Team Chat View — Lead Chat (Read + Steer)
 *
 * Matches Pencil design "PRAXIS — Lead Chat (Read+Steer)":
 * - Header: back arrow + domain dot + lead name + worker/turn badge + Stop button
 * - Lead message about task delegation
 * - WORKERS section with worker cards
 * - LEAD REVIEW section with review message + warning badge
 * - Steer input at bottom
 */

import { ArrowLeft, CircleCheck, Loader, Square, TriangleAlert, Users, XCircle } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, onCleanup, Show } from 'solid-js'
import { useTeam } from '../../stores/team'
import { DOMAIN_COLORS } from '../../stores/team-helpers'
import type { TeamMember } from '../../types/team'
import { TeamChatInput } from './TeamChatInput'
import { formatElapsed } from './tool-call-utils'

interface TeamChatViewProps {
  onStopAgent: (memberId: string) => void
  onSendMessage: (memberId: string, message: string) => void
}

export const TeamChatView: Component<TeamChatViewProps> = (props) => {
  const team = useTeam()
  const [_elapsed, setElapsed] = createSignal('')

  const member = (): TeamMember | null => team.selectedMember()
  const isWorking = () => member()?.status === 'working'

  const domainColor = () => DOMAIN_COLORS[member()?.domain ?? 'general']

  /** Children of the selected member (for leads). */
  const children = createMemo((): TeamMember[] => {
    const m = member()
    return m ? team.getChildren(m.id) : []
  })

  /** Get messages from the lead (assistant role = lead thinking). */
  const leadMessages = createMemo(() => {
    const m = member()
    return m ? m.messages.filter((msg) => msg.role === 'assistant') : []
  })

  /** Worker count + turn summary for header badge */
  const headerBadge = createMemo(() => {
    const m = member()
    if (!m) return ''
    const workerCount = children().length
    const doneCalls = m.toolCalls.filter((tc) => tc.status === 'success').length
    const totalCalls = m.toolCalls.length
    if (workerCount > 0) {
      return `${workerCount} worker${workerCount !== 1 ? 's' : ''} \u00B7 ${doneCalls}/${totalCalls}`
    }
    return `${doneCalls}/${totalCalls}`
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
    <div class="flex flex-col h-full min-h-0" style={{ background: 'var(--background)' }}>
      {/* Header: back arrow + domain dot + name + badge + Stop */}
      <Show when={member()}>
        {(m) => (
          <div
            class="flex items-center justify-between h-11 px-4"
            style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
          >
            {/* Left: back + dot + name + badge */}
            <div class="flex items-center gap-2.5">
              <button
                type="button"
                onClick={() => team.navigateBack()}
                class="text-[var(--gray-7)] hover:text-[var(--gray-9)] transition-colors"
                aria-label="Back to team overview"
              >
                <ArrowLeft class="w-4 h-4" />
              </button>
              <span
                class="w-2.5 h-2.5 rounded-full flex-shrink-0"
                style={{ background: domainColor() }}
              />
              <span class="text-[13px] font-semibold text-[var(--text-primary)]">{m().name}</span>
              <Show when={headerBadge()}>
                <span
                  class="text-[9px] font-medium px-1.5 py-0.5 rounded"
                  style={{ color: domainColor(), background: `${domainColor()}20` }}
                >
                  {headerBadge()}
                </span>
              </Show>
            </div>

            {/* Right: Stop button */}
            <Show when={isWorking()}>
              <button
                type="button"
                onClick={() => props.onStopAgent(m().id)}
                class="flex items-center gap-1 px-2.5 py-1 rounded-md text-[11px] font-medium text-[var(--error)] transition-colors"
                style={{ background: '#EF444420' }}
                title="Stop this agent"
                aria-label={`Stop ${m().name}`}
              >
                <Square class="w-2.5 h-2.5" />
                Stop
              </button>
            </Show>
          </div>
        )}
      </Show>

      {/* Main scrollable content — justified to bottom */}
      <div class="flex-1 overflow-y-auto scrollbar-thin flex flex-col justify-end px-5 py-4 gap-3">
        <Show
          when={member()}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-[var(--text-muted)] text-[13px]">
              <Users class="w-8 h-8 mb-2 opacity-40" />
              <span>Waiting for activity...</span>
            </div>
          }
        >
          {/* Lead messages */}
          <For each={leadMessages()}>
            {(msg) => (
              <p class="text-[13px] text-[var(--gray-10)] leading-relaxed">{msg.content}</p>
            )}
          </For>

          {/* WORKERS section */}
          <Show when={children().length > 0}>
            <div
              class="text-[9px] font-semibold mt-2"
              style={{ color: 'var(--gray-6)', 'letter-spacing': '0.8px' }}
            >
              WORKERS
            </div>

            <div class="space-y-3">
              <For each={children()}>
                {(child) => (
                  <WorkerChatCard
                    worker={child}
                    onNavigate={(id) => team.setSelectedMemberId(id)}
                  />
                )}
              </For>
            </div>
          </Show>

          {/* LEAD REVIEW section */}
          <Show when={member()?.result}>
            <div class="w-full h-px" style={{ background: 'var(--border-subtle)' }} />
            <div
              class="text-[9px] font-semibold"
              style={{ color: 'var(--gray-6)', 'letter-spacing': '0.8px' }}
            >
              LEAD REVIEW
            </div>
            <p class="text-[13px] text-[var(--gray-10)] leading-relaxed">{member()!.result}</p>
          </Show>

          {/* Error state */}
          <Show when={member()?.error}>
            <div
              class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-lg"
              style={{ background: '#F59E0B20' }}
            >
              <TriangleAlert class="w-3 h-3" style={{ color: '#F59E0B' }} />
              <span class="text-[11px]" style={{ color: '#F59E0B' }}>
                {member()!.error}
              </span>
            </div>
          </Show>
        </Show>
      </div>

      {/* Steer input */}
      <TeamChatInput onSendMessage={props.onSendMessage} />
    </div>
  )
}

// ============================================================================
// Worker Chat Card — matches Pencil design
// ============================================================================

const WorkerChatCard: Component<{
  worker: TeamMember
  onNavigate: (id: string) => void
}> = (props) => {
  const domainColor = () => DOMAIN_COLORS[props.worker.domain]

  const statusBadge = (): { text: string; color: string } => {
    const done = props.worker.toolCalls.filter((tc) => tc.status === 'success').length
    const total = props.worker.toolCalls.length
    const turnInfo = total > 0 ? ` \u00B7 ${done}/${total}` : ''

    switch (props.worker.status) {
      case 'working':
        return { text: `working${turnInfo}`, color: domainColor() }
      case 'done':
        return { text: `done${turnInfo}`, color: 'var(--success)' }
      case 'error':
        return { text: `error${turnInfo}`, color: 'var(--error)' }
      default:
        return { text: props.worker.status, color: 'var(--text-muted)' }
    }
  }

  return (
    <div
      class="rounded-xl p-3 flex flex-col gap-2"
      style={{ background: 'var(--gray-3)', border: '1px solid var(--border-subtle)' }}
    >
      {/* Header: dot + name + status badge */}
      <div class="flex items-center justify-between">
        <div class="flex items-center gap-2">
          <span class="w-2 h-2 rounded-full flex-shrink-0" style={{ background: domainColor() }} />
          <span class="text-[12px] font-medium text-[var(--text-primary)]">
            {props.worker.name}
          </span>
        </div>
        <span
          class="text-[9px] font-medium px-1.5 py-0.5 rounded"
          style={{
            color: statusBadge().color,
            background: `${statusBadge().color}20`,
          }}
        >
          {statusBadge().text}
        </span>
      </div>

      {/* Task */}
      <Show when={props.worker.task}>
        <p class="text-[11px] text-[var(--text-muted)]">{props.worker.task}</p>
      </Show>

      {/* Tool calls */}
      <Show when={props.worker.toolCalls.length > 0}>
        <div class="flex flex-col gap-1">
          <For each={props.worker.toolCalls}>
            {(tc) => (
              <div class="flex items-center gap-1.5">
                <Show
                  when={tc.status === 'success'}
                  fallback={
                    <Show
                      when={tc.status === 'running'}
                      fallback={<XCircle class="w-3 h-3 text-[var(--error)]" />}
                    >
                      <Loader class="w-3 h-3" style={{ color: 'var(--accent)' }} />
                    </Show>
                  }
                >
                  <CircleCheck class="w-3 h-3 text-[var(--success)]" />
                </Show>
                <span
                  class="text-[10px] font-['JetBrains_Mono',monospace]"
                  style={{
                    color: tc.status === 'running' ? 'var(--accent)' : 'var(--gray-7)',
                  }}
                >
                  {tc.name}
                  {tc.args && typeof tc.args === 'object' && 'path' in tc.args
                    ? ` ${String(tc.args.path).split('/').pop()}`
                    : ''}
                </span>
              </div>
            )}
          </For>
        </div>
      </Show>

      {/* View chat link */}
      <button
        type="button"
        onClick={() => props.onNavigate(props.worker.id)}
        class="text-left text-[10px] font-medium hover:underline transition-colors"
        style={{ color: 'var(--accent)' }}
        aria-label={`View ${props.worker.name}'s chat`}
      >
        View {props.worker.name.split(' ')[0]}'s chat &rarr;
      </button>
    </div>
  )
}
