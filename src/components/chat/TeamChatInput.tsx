/**
 * Team Chat Input
 *
 * Minimal input box for sending follow-up messages to a running team member.
 * Only enabled when the member's status is 'working'.
 * Emits a 'team:message' event that the agent-team-bridge picks up.
 */

import { Send } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import { useTeam } from '../../stores/team'

interface TeamChatInputProps {
  onSendMessage: (memberId: string, message: string) => void
}

export const TeamChatInput: Component<TeamChatInputProps> = (props) => {
  const team = useTeam()
  const [text, setText] = createSignal('')

  const member = () => team.selectedMember()
  const isWorking = () => member()?.status === 'working'

  const handleSend = () => {
    const m = member()
    const msg = text().trim()
    if (!m || !msg || !isWorking()) return

    props.onSendMessage(m.id, msg)
    setText('')
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSend()
    }
  }

  return (
    <Show when={member()}>
      <div class="flex items-center gap-2 px-3 py-2 border-t border-[var(--border-subtle)] bg-[var(--bg-subtle)]">
        <input
          type="text"
          value={text()}
          onInput={(e) => setText(e.currentTarget.value)}
          onKeyDown={handleKeyDown}
          disabled={!isWorking()}
          placeholder={
            isWorking()
              ? `Steer ${member()!.name.split(' ')[0]}... (Director will relay)`
              : `${member()!.name} is not running`
          }
          class="flex-1 bg-[var(--bg-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] px-3 py-1.5 text-[13px] text-[var(--text-primary)] placeholder:text-[var(--text-muted)] outline-none transition-colors duration-[var(--duration-fast)] focus:border-[var(--accent)]/50 disabled:opacity-50 disabled:cursor-not-allowed"
        />
        <button
          type="button"
          onClick={handleSend}
          disabled={!isWorking() || !text().trim()}
          class="p-1.5 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--accent)] hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)] disabled:opacity-30 disabled:cursor-not-allowed"
        >
          <Send class="w-4 h-4" />
        </button>
      </div>
    </Show>
  )
}
