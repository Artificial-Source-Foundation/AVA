import { ArrowLeft, Send, User } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'
import type { IssuePriority, KanbanColumn } from '../../../types/hq'

function statusInfo(s: KanbanColumn): { label: string; color: string } {
  switch (s) {
    case 'backlog':
      return { label: 'Backlog', color: '#3b82f6' }
    case 'in-progress':
      return { label: 'In Progress', color: '#06b6d4' }
    case 'review':
      return { label: 'Review', color: '#eab308' }
    case 'done':
      return { label: 'Done', color: 'var(--success)' }
  }
}

function priorityLabel(p: IssuePriority): { label: string; color: string } {
  switch (p) {
    case 'urgent':
      return { label: 'Urgent', color: 'var(--error)' }
    case 'high':
      return { label: 'High', color: 'var(--warning)' }
    case 'medium':
      return { label: 'Medium', color: 'var(--text-secondary)' }
    case 'low':
      return { label: 'Low', color: 'var(--text-muted)' }
  }
}

const HqIssueDetail: Component = () => {
  const { selectedIssue, navigateBack, addIssueComment } = useHq()
  const [commentText, setCommentText] = createSignal('')

  const handleComment = (): void => {
    const issue = selectedIssue()
    const content = commentText().trim()
    if (!issue || !content) return
    setCommentText('')
    void addIssueComment(issue.id, content)
  }

  return (
    <div class="flex flex-col h-full" style={{ 'background-color': 'var(--background)' }}>
      <Show
        when={selectedIssue()}
        fallback={
          <div
            class="flex items-center justify-center h-full"
            style={{ color: 'var(--text-muted)' }}
          >
            No issue selected
          </div>
        }
      >
        {(issue) => {
          const si = statusInfo(issue().status)
          const pi = priorityLabel(issue().priority)
          return (
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
                <span class="text-xs font-mono" style={{ color: 'var(--text-muted)' }}>
                  {issue().identifier}
                </span>
                <span class="text-sm font-semibold flex-1" style={{ color: 'var(--text-primary)' }}>
                  {issue().title}
                </span>
                <span
                  class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
                  style={{ color: si.color, 'background-color': `${si.color}22` }}
                >
                  {si.label}
                </span>
              </div>

              {/* Two-column layout */}
              <div class="flex flex-1 overflow-hidden">
                {/* Left: content */}
                <div
                  class="flex-1 overflow-y-auto px-6 py-4"
                  style={{ display: 'flex', 'flex-direction': 'column', gap: '16px' }}
                >
                  {/* Description */}
                  <p class="text-sm leading-relaxed" style={{ color: 'var(--text-secondary)' }}>
                    {issue().description}
                  </p>

                  <div class="h-px" style={{ 'background-color': 'var(--border-subtle)' }} />

                  {/* Activity */}
                  <div class="flex items-center gap-2">
                    <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                      Activity
                    </span>
                    <span class="text-[11px] font-mono" style={{ color: 'var(--text-muted)' }}>
                      {issue().comments.length}
                    </span>
                  </div>

                  {/* Comment thread */}
                  <div class="flex flex-col gap-3">
                    <For each={issue().comments}>
                      {(comment) => (
                        <div class="flex gap-2.5">
                          <div
                            class="w-7 h-7 rounded-full flex items-center justify-center shrink-0"
                            style={{ 'background-color': 'var(--surface)' }}
                          >
                            <User size={12} style={{ color: 'var(--text-muted)' }} />
                          </div>
                          <div class="flex flex-col gap-1 min-w-0 flex-1">
                            <div class="flex items-center gap-2">
                              <span
                                class="text-xs font-semibold"
                                style={{ color: 'var(--text-primary)' }}
                              >
                                {comment.authorName}
                              </span>
                              <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                                {formatTime(comment.timestamp)}
                              </span>
                            </div>
                            <p
                              class="text-xs leading-relaxed"
                              style={{ color: 'var(--text-secondary)' }}
                            >
                              {comment.content}
                            </p>
                          </div>
                        </div>
                      )}
                    </For>
                  </div>

                  {/* Add comment */}
                  <div
                    class="flex items-center gap-2 rounded-lg px-3 h-10"
                    style={{
                      'background-color': 'var(--surface)',
                      border: '1px solid var(--border-subtle)',
                    }}
                  >
                    <input
                      type="text"
                      placeholder="Add a comment..."
                      value={commentText()}
                      onInput={(e) => setCommentText(e.currentTarget.value)}
                      class="flex-1 bg-transparent text-xs outline-none"
                      style={{ color: 'var(--text-primary)' }}
                    />
                    <button
                      type="button"
                      class="flex items-center justify-center w-6 h-6 rounded"
                      style={{ 'background-color': 'var(--accent)' }}
                      onClick={handleComment}
                    >
                      <Send size={12} style={{ color: 'white' }} />
                    </button>
                  </div>
                </div>

                {/* Right: properties panel */}
                <div
                  class="w-[300px] shrink-0 overflow-y-auto px-5 py-4"
                  style={{
                    'border-left': '1px solid var(--border-subtle)',
                    display: 'flex',
                    'flex-direction': 'column',
                    gap: '14px',
                  }}
                >
                  {/* Status */}
                  <PropertyRow label="Status">
                    <span
                      class="text-[10px] font-semibold px-1.5 py-0.5 rounded"
                      style={{ color: si.color, 'background-color': `${si.color}22` }}
                    >
                      {si.label}
                    </span>
                  </PropertyRow>

                  {/* Priority */}
                  <PropertyRow label="Priority">
                    <span class="text-xs font-medium" style={{ color: pi.color }}>
                      {pi.label}
                    </span>
                  </PropertyRow>

                  {/* Assignee */}
                  <PropertyRow label="Assignee">
                    <Show
                      when={issue().assigneeName}
                      fallback={
                        <span class="text-xs" style={{ color: 'var(--text-muted)' }}>
                          Unassigned
                        </span>
                      }
                    >
                      <div class="flex items-center gap-1.5">
                        <User size={12} style={{ color: 'var(--text-muted)' }} />
                        <span class="text-xs" style={{ color: 'var(--text-primary)' }}>
                          {issue().assigneeName}
                        </span>
                      </div>
                    </Show>
                  </PropertyRow>

                  {/* Epic */}
                  <PropertyRow label="Epic">
                    <span class="text-xs" style={{ color: 'var(--text-secondary)' }}>
                      {issue().epicId}
                    </span>
                  </PropertyRow>

                  {/* Phase */}
                  <Show when={issue().phaseLabel}>
                    <PropertyRow label="Phase">
                      <span class="text-xs" style={{ color: 'var(--text-secondary)' }}>
                        {issue().phaseLabel}
                      </span>
                    </PropertyRow>
                  </Show>

                  <div class="h-px" style={{ 'background-color': 'var(--border-subtle)' }} />

                  {/* Agent progress */}
                  <Show when={issue().agentProgress}>
                    {(progress) => (
                      <div class="flex flex-col gap-2">
                        <span
                          class="text-[11px] font-semibold"
                          style={{ color: 'var(--text-muted)' }}
                        >
                          Agent Progress
                        </span>
                        <div
                          class="w-full h-1.5 rounded-full overflow-hidden"
                          style={{ 'background-color': 'var(--surface)' }}
                        >
                          <div
                            class="h-full rounded-full"
                            style={{
                              width: `${(progress().turn / progress().maxTurns) * 100}%`,
                              'background-color': '#06b6d4',
                            }}
                          />
                        </div>
                        <span class="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                          Turn {progress().turn}/{progress().maxTurns}
                        </span>
                        <Show when={issue().agentLiveAction}>
                          <span class="text-[10px]" style={{ color: '#06b6d4' }}>
                            {issue().agentLiveAction}
                          </span>
                        </Show>
                      </div>
                    )}
                  </Show>

                  <div class="h-px" style={{ 'background-color': 'var(--border-subtle)' }} />

                  {/* Files changed */}
                  <Show when={issue().filesChanged.length > 0}>
                    <div class="flex flex-col gap-2">
                      <span
                        class="text-[11px] font-semibold"
                        style={{ color: 'var(--text-muted)' }}
                      >
                        Files Changed
                      </span>
                      <For each={issue().filesChanged}>
                        {(file) => (
                          <div class="flex items-center gap-2">
                            <span
                              class="text-[10px] font-mono flex-1 truncate"
                              style={{ color: 'var(--text-secondary)' }}
                            >
                              {file.path}
                            </span>
                            <span class="text-[9px] font-mono" style={{ color: 'var(--success)' }}>
                              +{file.additions}
                            </span>
                            <Show when={file.deletions > 0}>
                              <span class="text-[9px] font-mono" style={{ color: 'var(--error)' }}>
                                -{file.deletions}
                              </span>
                            </Show>
                          </div>
                        )}
                      </For>
                    </div>
                  </Show>
                </div>
              </div>
            </>
          )
        }}
      </Show>
    </div>
  )
}

interface PropertyRowProps {
  label: string
  children: import('solid-js').JSX.Element
}

const PropertyRow: Component<PropertyRowProps> = (props) => (
  <div class="flex items-center justify-between">
    <span class="text-[11px] font-medium" style={{ color: 'var(--text-muted)' }}>
      {props.label}
    </span>
    {props.children}
  </div>
)

function formatTime(timestamp: number): string {
  const seconds = Math.floor((Date.now() - timestamp) / 1000)
  if (seconds < 60) return 'just now'
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  return `${hours}h ago`
}

export default HqIssueDetail
