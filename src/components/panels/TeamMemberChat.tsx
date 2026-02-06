/**
 * Team Member Chat Component
 *
 * Scoped chat view for a specific team member.
 * Shows their messages, tool calls, and allows sending instructions.
 */

import { AlertCircle, ArrowLeft, CheckCircle2, Crown, Loader2, Send, Wrench } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useTeam } from '../../stores/team'
import { TEAM_DOMAINS, type TeamMember, type TeamToolCall } from '../../types/team'

// ============================================================================
// Tool Call Row
// ============================================================================

const ToolCallRow: Component<{ call: TeamToolCall }> = (props) => {
  const statusColor = () => {
    switch (props.call.status) {
      case 'running':
        return 'var(--accent)'
      case 'success':
        return 'var(--success)'
      case 'error':
        return 'var(--error)'
    }
  }

  return (
    <div class="flex items-center gap-2 px-3 py-1">
      <Wrench class="w-3 h-3 flex-shrink-0" style={{ color: statusColor() }} />
      <span class="font-[var(--font-ui-mono)] text-[10px] tracking-wide text-[var(--text-primary)]">
        {props.call.name}
      </span>
      <Show when={props.call.status === 'running'}>
        <Loader2 class="w-2.5 h-2.5 animate-spin" style={{ color: statusColor() }} />
      </Show>
      <Show when={props.call.status === 'success'}>
        <CheckCircle2 class="w-2.5 h-2.5" style={{ color: statusColor() }} />
      </Show>
      <Show when={props.call.status === 'error'}>
        <AlertCircle class="w-2.5 h-2.5" style={{ color: statusColor() }} />
      </Show>
      <Show when={props.call.durationMs}>
        <span class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-muted)] tabular-nums ml-auto">
          {props.call.durationMs! < 1000
            ? `${props.call.durationMs}ms`
            : `${(props.call.durationMs! / 1000).toFixed(1)}s`}
        </span>
      </Show>
    </div>
  )
}

// ============================================================================
// Main Component
// ============================================================================

interface TeamMemberChatProps {
  member: TeamMember
  onBack: () => void
}

export const TeamMemberChat: Component<TeamMemberChatProps> = (props) => {
  const team = useTeam()
  const [instruction, setInstruction] = createSignal('')
  const config = () => TEAM_DOMAINS[props.member.domain]
  const memberTeam = () => team.getMemberTeam(props.member.id)
  const isWorking = () => props.member.status === 'working'

  const handleSendInstruction = () => {
    const text = instruction().trim()
    if (!text) return

    // Add as a user message to this member
    team.addMessage(props.member.id, {
      id: `msg-${Date.now()}`,
      role: 'user',
      content: text,
      timestamp: Date.now(),
    })

    setInstruction('')
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSendInstruction()
    }
  }

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center gap-2 px-3 h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
        <button
          type="button"
          onClick={() => props.onBack()}
          class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
        >
          <ArrowLeft class="w-3.5 h-3.5" />
        </button>

        {/* Member identity */}
        <span
          class="w-1.5 h-1.5 rounded-full flex-shrink-0"
          style={{
            background: isWorking()
              ? config().color
              : props.member.status === 'done'
                ? 'var(--success)'
                : 'var(--gray-6)',
          }}
        />
        <div class="flex-1 min-w-0 flex items-center gap-1.5">
          <Show when={props.member.role === 'senior-lead' || props.member.role === 'team-lead'}>
            <Crown class="w-3 h-3 flex-shrink-0" style={{ color: config().color }} />
          </Show>
          <span class="font-[var(--font-ui-mono)] text-[11px] tracking-wide font-semibold text-[var(--text-primary)] truncate">
            {props.member.name}
          </span>
        </div>

        {/* Team badge */}
        <Show when={memberTeam()}>
          <span
            class="font-[var(--font-ui-mono)] text-[9px] tracking-widest px-1.5 py-px rounded-[var(--radius-sm)] font-medium"
            style={{ background: config().colorSubtle, color: config().color }}
          >
            {config().short}
          </span>
        </Show>
      </div>

      {/* Task description */}
      <Show when={props.member.task}>
        <div class="px-3 py-2 border-b border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
          <p class="text-[10px] text-[var(--text-secondary)]">
            <span class="text-[var(--text-muted)]">Task: </span>
            {props.member.task}
          </p>
        </div>
      </Show>

      {/* Messages + Tool Calls */}
      <div class="flex-1 overflow-y-auto scrollbar-none">
        <Show
          when={props.member.messages.length > 0 || props.member.toolCalls.length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <p class="text-[10px] text-[var(--text-muted)]">
                {isWorking() ? 'Working...' : 'No messages yet'}
              </p>
            </div>
          }
        >
          {/* Interleave messages and tool calls by timestamp */}
          <div class="py-2 space-y-1.5">
            <For each={props.member.messages}>
              {(msg) => (
                <div class={`px-3 py-2 ${msg.role === 'user' ? 'bg-[var(--accent-subtle)]' : ''}`}>
                  <div class="font-[var(--font-ui-mono)] text-[9px] tracking-wider text-[var(--text-muted)] mb-0.5 uppercase">
                    {msg.role === 'user' ? 'You' : props.member.name}
                  </div>
                  <p class="text-[12px] text-[var(--text-primary)] whitespace-pre-wrap break-words leading-relaxed">
                    {msg.content}
                  </p>
                </div>
              )}
            </For>
          </div>

          {/* Tool calls section */}
          <Show when={props.member.toolCalls.length > 0}>
            <div class="border-t border-[var(--border-subtle)]">
              <div class="px-3 py-1.5">
                <span class="font-[var(--font-ui-mono)] text-[9px] tracking-widest text-[var(--text-muted)] uppercase">
                  Tool Calls
                </span>
              </div>
              <For each={props.member.toolCalls}>{(call) => <ToolCallRow call={call} />}</For>
            </div>
          </Show>
        </Show>
      </div>

      {/* Input */}
      <div class="px-2 py-2 border-t border-[var(--border-subtle)]">
        <div class="flex items-center gap-1.5">
          <input
            type="text"
            value={instruction()}
            onInput={(e) => setInstruction(e.currentTarget.value)}
            onKeyDown={handleKeyDown}
            placeholder={`Instruct ${props.member.name}...`}
            class="
              flex-1
              px-2.5 py-1.5
              bg-[var(--surface-glass)]
              border border-[var(--border-default)]
              rounded-[var(--radius-md)]
              font-[var(--font-ui-mono)] text-[11px]
              text-[var(--text-primary)]
              placeholder:text-[var(--text-muted)]
              focus:outline-none focus:border-[var(--accent)]
              transition-colors
            "
          />
          <button
            type="button"
            onClick={handleSendInstruction}
            disabled={!instruction().trim()}
            class="
              p-1.5
              rounded-[var(--radius-md)]
              text-[var(--text-muted)]
              hover:text-[var(--accent)] hover:bg-[var(--accent-subtle)]
              disabled:opacity-30 disabled:cursor-not-allowed
              transition-colors
            "
          >
            <Send class="w-3.5 h-3.5" />
          </button>
        </div>
      </div>

      {/* Error display */}
      <Show when={props.member.error}>
        <div class="px-3 py-2 bg-[var(--error-subtle)] border-t border-[var(--error-border)]">
          <div class="flex items-center gap-1.5">
            <AlertCircle class="w-3 h-3 text-[var(--error)] flex-shrink-0" />
            <span class="text-[10px] text-[var(--error)]">{props.member.error}</span>
          </div>
        </div>
      </Show>
    </div>
  )
}
