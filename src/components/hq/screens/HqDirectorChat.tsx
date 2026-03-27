import { Crown, Pause, Send, Square } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'

const HqDirectorChat: Component = () => {
  const { directorMessages, sendDirectorMessage } = useHq()
  const [steerText, setSteerText] = createSignal('')

  const handleSend = (): void => {
    const value = steerText().trim()
    if (!value) return
    setSteerText('')
    void sendDirectorMessage(value)
  }

  function statusDot(status: string): string {
    if (status === 'running') return '#06b6d4'
    if (status === 'done') return 'var(--success)'
    return 'var(--text-muted)'
  }

  return (
    <div class="flex flex-col h-full" style={{ 'background-color': 'var(--background)' }}>
      {/* Header */}
      <div
        class="flex items-center justify-between shrink-0 px-6 h-14"
        style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
      >
        <div class="flex items-center gap-2.5">
          <Crown size={18} style={{ color: '#f59e0b' }} />
          <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
            Director (Opus)
          </span>
          <span
            class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
            style={{ color: 'var(--success)', 'background-color': 'rgba(34,197,94,0.15)' }}
          >
            active
          </span>
        </div>
        <div class="flex items-center gap-1.5">
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md"
            style={{
              'background-color': 'var(--surface)',
              border: '1px solid var(--border-subtle)',
            }}
            title="Pause"
          >
            <Pause size={14} style={{ color: 'var(--text-secondary)' }} />
          </button>
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md"
            style={{
              'background-color': 'var(--surface)',
              border: '1px solid var(--border-subtle)',
            }}
            title="Stop"
          >
            <Square size={14} style={{ color: 'var(--error)' }} />
          </button>
        </div>
      </div>

      {/* Messages */}
      <div
        class="flex-1 overflow-y-auto px-6 py-4"
        style={{ gap: '16px', display: 'flex', 'flex-direction': 'column' }}
      >
        <For each={directorMessages()}>
          {(msg) => (
            <Show
              when={msg.role === 'director'}
              fallback={
                /* User message - right aligned */
                <div class="flex justify-end">
                  <div
                    class="max-w-[70%] rounded-xl px-4 py-2.5"
                    style={{
                      'background-color': 'rgba(139,92,246,0.2)',
                      color: 'var(--text-primary)',
                    }}
                  >
                    <span class="text-sm leading-relaxed">{msg.content}</span>
                    <div class="text-right mt-1">
                      <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                        {formatTime(msg.timestamp)}
                      </span>
                    </div>
                  </div>
                </div>
              }
            >
              {/* Director message - left aligned with crown avatar */}
              <div class="flex gap-3 max-w-[85%]">
                <div
                  class="w-8 h-8 rounded-full flex items-center justify-center shrink-0"
                  style={{ 'background-color': 'rgba(245,158,11,0.15)' }}
                >
                  <Crown size={14} style={{ color: '#f59e0b' }} />
                </div>
                <div class="flex flex-col gap-2 min-w-0">
                  <div
                    class="rounded-xl px-4 py-2.5"
                    style={{
                      'background-color': 'var(--surface)',
                      border: '1px solid var(--border-subtle)',
                    }}
                  >
                    <span class="text-sm leading-relaxed" style={{ color: 'var(--text-primary)' }}>
                      {msg.content}
                    </span>
                  </div>

                  {/* Delegation cards */}
                  <Show when={msg.delegations && msg.delegations.length > 0}>
                    <div class="flex flex-col gap-1.5">
                      <For each={msg.delegations}>
                        {(d) => (
                          <div
                            class="flex items-center gap-2.5 px-3 py-2 rounded-lg"
                            style={{
                              'background-color': 'var(--surface)',
                              border: '1px solid var(--border-subtle)',
                            }}
                          >
                            <div
                              class="w-2 h-2 rounded-full shrink-0"
                              style={{ 'background-color': statusDot(d.status) }}
                            />
                            <span
                              class="text-xs font-semibold"
                              style={{ color: 'var(--text-primary)' }}
                            >
                              {d.agentName}
                            </span>
                            <span class="text-xs flex-1" style={{ color: 'var(--text-muted)' }}>
                              {d.task}
                            </span>
                            <span
                              class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
                              style={{
                                color: statusDot(d.status),
                                'background-color':
                                  d.status === 'running'
                                    ? 'rgba(6,182,212,0.15)'
                                    : d.status === 'done'
                                      ? 'rgba(34,197,94,0.15)'
                                      : 'rgba(161,161,170,0.15)',
                              }}
                            >
                              {d.status}
                            </span>
                          </div>
                        )}
                      </For>
                    </div>
                  </Show>

                  <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                    {formatTime(msg.timestamp)}
                  </span>
                </div>
              </div>
            </Show>
          )}
        </For>
      </div>

      {/* Steering input */}
      <div class="shrink-0 px-6 py-3" style={{ 'border-top': '1px solid var(--border-subtle)' }}>
        <div
          class="flex items-center gap-2 rounded-lg px-3 h-10"
          style={{ 'background-color': 'var(--surface)', border: '1px solid var(--border-subtle)' }}
        >
          <input
            type="text"
            placeholder="Steer the Director..."
            value={steerText()}
            onInput={(e) => setSteerText(e.currentTarget.value)}
            class="flex-1 bg-transparent text-sm outline-none"
            style={{ color: 'var(--text-primary)' }}
          />
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md"
            style={{ 'background-color': 'var(--accent)' }}
            onClick={handleSend}
          >
            <Send size={14} style={{ color: 'white' }} />
          </button>
        </div>
      </div>
    </div>
  )
}

function formatTime(timestamp: number): string {
  const seconds = Math.floor((Date.now() - timestamp) / 1000)
  if (seconds < 60) return 'just now'
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  return `${hours}h ago`
}

export default HqDirectorChat
