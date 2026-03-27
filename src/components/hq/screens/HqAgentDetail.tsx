import { ArrowLeft, Send, Terminal, User } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'

const HqAgentDetail: Component = () => {
  const { selectedAgent, issues, navigateBack, steerAgent } = useHq()
  const [steerText, setSteerText] = createSignal('')

  const handleSend = (): void => {
    const agent = selectedAgent()
    const message = steerText().trim()
    if (!agent || !message) return
    setSteerText('')
    void steerAgent(agent.id, message)
  }

  const assignedIssues = () => {
    const agent = selectedAgent()
    if (!agent) return []
    return issues().filter((i) => agent.assignedIssueIds.includes(i.id))
  }

  function toolStatusColor(status?: string): string {
    if (status === 'running') return '#06b6d4'
    if (status === 'done') return 'var(--success)'
    return 'var(--error)'
  }

  return (
    <div class="flex flex-col h-full" style={{ 'background-color': 'var(--background)' }}>
      <Show
        when={selectedAgent()}
        fallback={
          <div
            class="flex items-center justify-center h-full"
            style={{ color: 'var(--text-muted)' }}
          >
            No agent selected
          </div>
        }
      >
        {(agent) => (
          <>
            {/* Header */}
            <div
              class="flex items-center gap-3 shrink-0 px-6 h-14"
              style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
            >
              <button
                type="button"
                class="flex items-center justify-center w-7 h-7 rounded-md"
                style={{
                  'background-color': 'var(--surface)',
                  border: '1px solid var(--border-subtle)',
                }}
                onClick={navigateBack}
              >
                <ArrowLeft size={14} style={{ color: 'var(--text-secondary)' }} />
              </button>
              <div
                class="w-8 h-8 rounded-full flex items-center justify-center"
                style={{ 'background-color': 'var(--surface)' }}
              >
                <User size={14} style={{ color: 'var(--text-muted)' }} />
              </div>
              <div class="flex flex-col">
                <div class="flex items-center gap-2">
                  <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                    {agent().name}
                  </span>
                  <span class="text-xs" style={{ color: 'var(--text-muted)' }}>
                    {agent().role}
                  </span>
                </div>
                <div class="flex items-center gap-2">
                  <Show when={agent().currentIssueId}>
                    <span class="text-[10px] font-mono" style={{ color: '#06b6d4' }}>
                      Working on {agent().currentIssueId}
                    </span>
                  </Show>
                  <Show when={agent().turn != null && agent().maxTurns != null}>
                    <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                      Turn {agent().turn}/{agent().maxTurns}
                    </span>
                  </Show>
                </div>
              </div>
            </div>

            {/* Main content + right panel */}
            <div class="flex flex-1 overflow-hidden">
              {/* Transcript viewer */}
              <div class="flex-1 flex flex-col overflow-hidden">
                <div
                  class="flex-1 overflow-y-auto px-6 py-4"
                  style={{ display: 'flex', 'flex-direction': 'column', gap: '10px' }}
                >
                  <Show when={agent().transcript.length === 0}>
                    <div
                      class="flex items-center justify-center h-32"
                      style={{ color: 'var(--text-muted)' }}
                    >
                      <span class="text-xs">No transcript entries yet</span>
                    </div>
                  </Show>
                  <For each={agent().transcript}>
                    {(entry) => (
                      <Show
                        when={entry.type === 'tool-call'}
                        fallback={
                          /* Message entry */
                          <div class="flex gap-2.5">
                            <div
                              class="w-6 h-6 rounded-full flex items-center justify-center shrink-0 mt-0.5"
                              style={{ 'background-color': 'var(--surface)' }}
                            >
                              <User size={10} style={{ color: 'var(--text-muted)' }} />
                            </div>
                            <p
                              class="text-xs leading-relaxed"
                              style={{ color: 'var(--text-secondary)' }}
                            >
                              {entry.content}
                            </p>
                          </div>
                        }
                      >
                        {/* Tool-call entry */}
                        <div
                          class="flex items-start gap-2.5 px-3 py-2 rounded-md"
                          style={{
                            'background-color': 'var(--surface)',
                            border: '1px solid var(--border-subtle)',
                          }}
                        >
                          <Terminal
                            size={13}
                            class="shrink-0 mt-0.5"
                            style={{ color: 'var(--text-muted)' }}
                          />
                          <div class="flex flex-col gap-1 flex-1 min-w-0">
                            <div class="flex items-center gap-2">
                              <span
                                class="text-[11px] font-mono font-semibold"
                                style={{ color: 'var(--text-primary)' }}
                              >
                                {entry.toolName}
                              </span>
                              <Show when={entry.toolPath}>
                                <span
                                  class="text-[10px] font-mono truncate"
                                  style={{ color: 'var(--text-muted)' }}
                                >
                                  {entry.toolPath}
                                </span>
                              </Show>
                              <span
                                class="text-[9px] font-semibold px-1.5 py-0.5 rounded ml-auto shrink-0"
                                style={{
                                  color: toolStatusColor(entry.toolStatus),
                                  'background-color': `${toolStatusColor(entry.toolStatus)}22`,
                                }}
                              >
                                {entry.toolStatus}
                              </span>
                            </div>
                            <span
                              class="text-[10px] leading-snug"
                              style={{ color: 'var(--text-muted)' }}
                            >
                              {entry.content}
                            </span>
                          </div>
                        </div>
                      </Show>
                    )}
                  </For>
                </div>

                {/* Steering input */}
                <div
                  class="shrink-0 px-6 py-3"
                  style={{ 'border-top': '1px solid var(--border-subtle)' }}
                >
                  <div
                    class="flex items-center gap-2 rounded-lg px-3 h-10"
                    style={{
                      'background-color': 'var(--surface)',
                      border: '1px solid var(--border-subtle)',
                    }}
                  >
                    <input
                      type="text"
                      placeholder={`Steer ${agent().name}...`}
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

              {/* Right panel */}
              <div
                class="w-[280px] shrink-0 overflow-y-auto px-4 py-4"
                style={{
                  'border-left': '1px solid var(--border-subtle)',
                  display: 'flex',
                  'flex-direction': 'column',
                  gap: '14px',
                }}
              >
                {/* Assigned tasks */}
                <span class="text-[11px] font-semibold" style={{ color: 'var(--text-muted)' }}>
                  Assigned Tasks
                </span>
                <Show when={assignedIssues().length === 0}>
                  <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                    No tasks assigned
                  </span>
                </Show>
                <For each={assignedIssues()}>
                  {(issue) => {
                    const statusColor =
                      issue.status === 'in-progress'
                        ? '#06b6d4'
                        : issue.status === 'done'
                          ? 'var(--success)'
                          : issue.status === 'review'
                            ? '#eab308'
                            : '#3b82f6'
                    return (
                      <div
                        class="flex flex-col gap-1.5 p-2.5 rounded-md"
                        style={{
                          'background-color': 'var(--surface)',
                          border: '1px solid var(--border-subtle)',
                        }}
                      >
                        <div class="flex items-center gap-1.5">
                          <div
                            class="w-1.5 h-1.5 rounded-full"
                            style={{ 'background-color': statusColor }}
                          />
                          <span
                            class="text-[10px] font-mono"
                            style={{ color: 'var(--text-muted)' }}
                          >
                            {issue.identifier}
                          </span>
                          <span
                            class="text-[9px] font-semibold px-1 py-0.5 rounded"
                            style={{ color: statusColor, 'background-color': `${statusColor}22` }}
                          >
                            {issue.status}
                          </span>
                        </div>
                        <span
                          class="text-[11px] font-medium"
                          style={{ color: 'var(--text-primary)' }}
                        >
                          {issue.title}
                        </span>
                        <Show when={issue.agentProgress}>
                          {(progress) => (
                            <div
                              class="w-full h-1 rounded-full overflow-hidden"
                              style={{ 'background-color': 'var(--background)' }}
                            >
                              <div
                                class="h-full rounded-full"
                                style={{
                                  width: `${(progress().turn / progress().maxTurns) * 100}%`,
                                  'background-color': statusColor,
                                }}
                              />
                            </div>
                          )}
                        </Show>
                      </div>
                    )
                  }}
                </For>

                <div class="h-px" style={{ 'background-color': 'var(--border-subtle)' }} />

                {/* Files touched */}
                <span class="text-[11px] font-semibold" style={{ color: 'var(--text-muted)' }}>
                  Files Touched
                </span>
                <Show when={agent().filesTouched.length === 0}>
                  <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                    No files touched
                  </span>
                </Show>
                <For each={agent().filesTouched}>
                  {(file) => (
                    <span class="text-[10px] font-mono" style={{ color: 'var(--text-secondary)' }}>
                      {file}
                    </span>
                  )}
                </For>
              </div>
            </div>
          </>
        )}
      </Show>
    </div>
  )
}

export default HqAgentDetail
