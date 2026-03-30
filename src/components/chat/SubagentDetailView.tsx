/**
 * Subagent Detail View
 *
 * Read-only chat view showing a delegated subagent's real-time activity.
 * Opens when clicking a SubagentCard in the main chat.
 *
 * Matches Pencil design node INAOZ:
 * - Purple-tinted header (56px): back arrow, bot icon, name+role, metadata, status pill, elapsed
 * - Read-only banner (32px): centered "Read-only -- this agent is working autonomously"
 * - Agent stream: scrollable, max-width 800px, shows messages + tool calls
 * - No composer -- purely observational
 */

import { ArrowLeft, Bot, Check, FileText, Loader2, Search, Terminal, XCircle } from 'lucide-solid'
import { type Component, createEffect, createMemo, For, on, Show } from 'solid-js'
import { useSecondTicker } from '../../hooks/useElapsedTimer'
import { formatElapsedSince } from '../../lib/format-time'
import { useLayout } from '../../stores/layout'
import { useTeam } from '../../stores/team'
import type { ToolCall } from '../../types'
import type { TeamMember, TeamMessage, TeamToolCall } from '../../types/team'
import { formatDuration } from './tool-call-utils'

// ============================================================================
// Helpers
// ============================================================================

/** Pick a lucide icon for a tool name */
function toolIcon(name: string): typeof Search {
  if (name === 'glob' || name === 'grep' || name === 'web_search' || name === 'codebase_search')
    return Search
  if (name === 'read' || name === 'write' || name === 'edit') return FileText
  if (name === 'bash') return Terminal
  return Search
}

/** Format tool call args into a short summary string */
function toolSummary(tc: TeamToolCall): string {
  if (!tc.args) return tc.name
  const path = tc.args.path ?? tc.args.file_path ?? tc.args.pattern ?? tc.args.command
  if (path) return `${tc.name} ${String(path)}`
  return tc.name
}

// ============================================================================
// Sub-components
// ============================================================================

/** A single tool call row in the agent stream (matches Pencil tool1/tool2/toolRunning) */
const StreamToolCall: Component<{ tc: TeamToolCall }> = (props) => {
  const isRunning = () => props.tc.status === 'running'
  const isSuccess = () => props.tc.status === 'success'
  const isError = () => props.tc.status === 'error'

  const nowTick = useSecondTicker(isRunning)

  const elapsed = createMemo(() => {
    if (!isRunning()) return ''
    nowTick()
    return formatElapsedSince(props.tc.startedAt)
  })

  const duration = () => {
    if (!props.tc.completedAt) return null
    return formatDuration(props.tc.completedAt - props.tc.startedAt)
  }

  const icon = createMemo(() => toolIcon(props.tc.name))

  return (
    <div
      class="flex items-center justify-between rounded-lg"
      style={{
        background: 'var(--background-subtle)',
        height: '36px',
        padding: '0 12px',
        border: `1px solid ${isRunning() ? 'var(--accent-border)' : 'var(--border-subtle)'}`,
      }}
    >
      {/* Left: icon + tool name */}
      <div class="flex items-center gap-1.5">
        <Show
          when={!isRunning()}
          fallback={
            <Loader2
              class="flex-shrink-0 animate-spin"
              style={{ width: '12px', height: '12px', color: 'var(--accent)' }}
            />
          }
        >
          {(() => {
            const Icon = icon()
            return (
              <Icon
                class="flex-shrink-0"
                style={{ width: '12px', height: '12px', color: 'var(--accent)' }}
              />
            )
          })()}
        </Show>
        <span
          class="truncate"
          style={{
            'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
            'font-size': '11px',
            'font-weight': '500',
            color: isRunning() ? 'var(--text-primary)' : 'var(--text-tertiary)',
          }}
        >
          {toolSummary(props.tc)}
        </span>
      </div>

      {/* Right: status icon + time */}
      <div class="flex items-center gap-1.5">
        <Show when={isSuccess()}>
          <Check
            class="flex-shrink-0"
            style={{ width: '12px', height: '12px', color: 'var(--success)' }}
          />
          <Show when={duration()}>
            <span
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '10px',
                color: 'var(--text-muted)',
              }}
            >
              {duration()}
            </span>
          </Show>
        </Show>
        <Show when={isError()}>
          <XCircle
            class="flex-shrink-0"
            style={{ width: '12px', height: '12px', color: 'var(--error)' }}
          />
        </Show>
        <Show when={isRunning()}>
          <span
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '10px',
              color: 'var(--accent)',
            }}
          >
            {elapsed()}
          </span>
        </Show>
      </div>
    </div>
  )
}

/** A text message in the agent stream */
const StreamMessage: Component<{ message: TeamMessage }> = (props) => (
  <p
    style={{
      'font-family': 'var(--font-ui), Geist, sans-serif',
      'font-size': '14px',
      'line-height': '1.5',
      color: 'var(--text-secondary)',
    }}
  >
    {props.message.content}
  </p>
)

// ============================================================================
// Main Component
// ============================================================================

interface SubagentDetailViewProps {
  /** The tool call ID that triggered this delegation */
  toolCallId: string
  /** The original ToolCall object (from the parent chat) */
  toolCall: ToolCall | undefined
}

export const SubagentDetailView: Component<SubagentDetailViewProps> = (props) => {
  const { closeSubagentDetail } = useLayout()
  const team = useTeam()

  let streamRef: HTMLDivElement | undefined

  // Match this tool call to a team member
  const matchedMember = createMemo((): TeamMember | null => {
    if (!props.toolCall) return null
    const args = props.toolCall.args
    const taskText = String(args.task ?? args.prompt ?? args.goal ?? '')
    const startedAt = props.toolCall.startedAt

    const delegationMatch = team
      .delegationLog()
      .filter((event) => event.task === taskText || event.task.startsWith(taskText.slice(0, 40)))
      .sort((a, b) => Math.abs(a.timestamp - startedAt) - Math.abs(b.timestamp - startedAt))[0]

    if (delegationMatch) {
      return team.allMembers().find((member) => member.id === delegationMatch.toMember) ?? null
    }

    if (!taskText) return null
    for (const member of team.allMembers()) {
      if (member.task && taskText.startsWith(member.task.slice(0, 40))) {
        return member
      }
    }
    return null
  })

  const isRunning = () => {
    const member = matchedMember()
    if (member) return member.status === 'working'
    return props.toolCall?.status === 'running' || props.toolCall?.status === 'pending'
  }

  const isDone = () => {
    const member = matchedMember()
    if (member) return member.status === 'done' || member.status === 'error'
    return props.toolCall?.status === 'success' || props.toolCall?.status === 'error'
  }

  const isError = () => {
    const member = matchedMember()
    if (member) return member.status === 'error'
    return props.toolCall?.status === 'error'
  }

  const nowTick = useSecondTicker(isRunning)

  // Agent name + role
  const agentName = () => {
    const member = matchedMember()
    if (member) return member.name
    if (!props.toolCall) return 'Subagent'
    const args = props.toolCall.args as Record<string, unknown>
    const role = String(args.role ?? args.agent_type ?? 'Scout')
    const goal = String(args.goal ?? args.description ?? args.prompt ?? 'task')
    const truncated = goal.length > 50 ? `${goal.slice(0, 47)}...` : goal
    return `${role} \u2014 ${truncated}`
  }

  // Metadata line: "haiku . read-only . delegated 12s ago"
  const metadata = createMemo(() => {
    const parts: string[] = []
    const member = matchedMember()
    if (member?.model) parts.push(member.model)
    else if (props.toolCall) {
      const args = props.toolCall.args as Record<string, unknown>
      if (args.model) parts.push(String(args.model))
    }

    if (props.toolCall) {
      const args = props.toolCall.args as Record<string, unknown>
      if (args.mode) parts.push(String(args.mode))
    }

    if (isRunning() && props.toolCall) {
      nowTick()
      parts.push(`delegated ${formatElapsedSince(props.toolCall.startedAt)} ago`)
    } else if (isDone() && props.toolCall?.completedAt) {
      const dur = formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
      parts.push(`completed in ${dur}`)
    }

    return parts.join(' \u00b7 ')
  })

  // Elapsed timer for header
  const elapsed = createMemo(() => {
    if (!isRunning() || !props.toolCall) return ''
    nowTick()
    return formatElapsedSince(props.toolCall.startedAt)
  })

  // Status badge text
  const statusText = () => {
    if (isRunning()) return 'Running'
    if (isError()) return 'Error'
    return 'Done'
  }

  const statusColor = () => {
    if (isError()) return 'var(--error)'
    if (isDone()) return 'var(--success)'
    return 'var(--accent)'
  }

  // Interleave messages and tool calls chronologically
  const streamItems = createMemo(() => {
    const member = matchedMember()
    if (!member) {
      // Fallback: show streaming output from the tool call itself
      return []
    }

    type StreamItem = { kind: 'message'; message: TeamMessage } | { kind: 'tool'; tc: TeamToolCall }

    const items: StreamItem[] = []

    // Collect all items with timestamps
    for (const msg of member.messages) {
      if (msg.role === 'assistant' && msg.content) {
        items.push({ kind: 'message', message: msg })
      }
    }
    for (const tc of member.toolCalls) {
      items.push({ kind: 'tool', tc })
    }

    // Sort by timestamp
    items.sort((a, b) => {
      const tsA = a.kind === 'message' ? a.message.timestamp : a.tc.startedAt
      const tsB = b.kind === 'message' ? b.message.timestamp : b.tc.startedAt
      return tsA - tsB
    })

    return items
  })

  // Auto-scroll to bottom when new items arrive
  createEffect(
    on(
      () => streamItems().length,
      () => {
        requestAnimationFrame(() => {
          if (streamRef) {
            streamRef.scrollTop = streamRef.scrollHeight
          }
        })
      }
    )
  )

  return (
    <div class="flex min-h-0 h-full flex-col bg-[var(--background)]">
      {/* ================================================================== */}
      {/* Header — 56px, purple-tinted                                      */}
      {/* ================================================================== */}
      <div
        class="flex items-center justify-between flex-shrink-0"
        style={{
          height: '56px',
          padding: '0 20px',
          background: 'color-mix(in srgb, var(--accent) 6%, transparent)',
          'border-bottom': '1px solid var(--accent-border)',
        }}
      >
        {/* Left: back + icon + name + meta */}
        <div class="flex items-center gap-3 min-w-0 flex-1">
          {/* Back arrow */}
          <button
            type="button"
            onClick={closeSubagentDetail}
            class="flex-shrink-0 rounded p-0.5 transition-colors hover:bg-[var(--alpha-white-8)]"
            aria-label="Back to chat"
          >
            <ArrowLeft style={{ width: '16px', height: '16px', color: 'var(--text-tertiary)' }} />
          </button>

          {/* Bot icon in purple container */}
          <div
            class="flex items-center justify-center flex-shrink-0"
            style={{
              width: '28px',
              height: '28px',
              'border-radius': '7px',
              background: 'var(--accent-subtle)',
            }}
          >
            <Bot style={{ width: '14px', height: '14px', color: 'var(--accent)' }} />
          </div>

          {/* Name + metadata */}
          <div class="flex flex-col gap-px min-w-0">
            <span
              class="truncate"
              style={{
                'font-family': 'var(--font-ui), Geist, sans-serif',
                'font-size': '14px',
                'font-weight': '500',
                color: 'var(--text-primary)',
              }}
            >
              {agentName()}
            </span>
            <Show when={metadata()}>
              <span
                class="truncate"
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '10px',
                  color: 'var(--accent)',
                }}
              >
                {metadata()}
              </span>
            </Show>
          </div>
        </div>

        {/* Right: status pill + elapsed */}
        <div class="flex items-center gap-2 flex-shrink-0">
          {/* Status badge */}
          <div
            class="flex items-center gap-1 rounded-md"
            style={{
              padding: '4px 8px',
              background: `${statusColor()}15`,
            }}
          >
            <Show when={isRunning()}>
              <Loader2
                class="animate-spin"
                style={{ width: '11px', height: '11px', color: statusColor() }}
              />
            </Show>
            <Show when={isDone() && !isError()}>
              <Check style={{ width: '11px', height: '11px', color: statusColor() }} />
            </Show>
            <Show when={isError()}>
              <XCircle style={{ width: '11px', height: '11px', color: statusColor() }} />
            </Show>
            <span
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '10px',
                'font-weight': '500',
                color: statusColor(),
              }}
            >
              {statusText()}
            </span>
          </div>

          {/* Elapsed time */}
          <Show when={elapsed()}>
            <span
              class="tabular-nums"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: 'var(--accent)',
              }}
            >
              {elapsed()}
            </span>
          </Show>
        </div>
      </div>

      {/* ================================================================== */}
      {/* Read-only banner — 32px                                            */}
      {/* ================================================================== */}
      <div
        class="flex items-center justify-center flex-shrink-0"
        style={{
          height: '32px',
          background: 'color-mix(in srgb, var(--accent) 8%, transparent)',
          padding: '0 20px',
        }}
      >
        <span
          style={{
            'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
            'font-size': '10px',
            color: 'color-mix(in srgb, var(--accent) 70%, transparent)',
          }}
        >
          Read-only — this agent is working autonomously
        </span>
      </div>

      {/* ================================================================== */}
      {/* Agent Stream — scrollable, max-width 800px, centered               */}
      {/* ================================================================== */}
      <div
        ref={streamRef}
        class="flex-1 overflow-y-auto scrollbar-thin"
        style={{ padding: '24px 0' }}
      >
        <div
          class="mx-auto flex flex-col gap-4"
          style={{ 'max-width': '800px', padding: '0 20px' }}
        >
          {/* Interleaved messages + tool calls */}
          <For each={streamItems()}>
            {(item) => (
              <Show
                when={item.kind === 'message'}
                fallback={<StreamToolCall tc={(item as { kind: 'tool'; tc: TeamToolCall }).tc} />}
              >
                <StreamMessage
                  message={(item as { kind: 'message'; message: TeamMessage }).message}
                />
              </Show>
            )}
          </For>

          {/* Fallback: show streaming output when no matched team member */}
          <Show when={!matchedMember() && props.toolCall?.streamingOutput}>
            <pre
              class="whitespace-pre-wrap break-all"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: 'var(--text-tertiary)',
                'line-height': '1.6',
              }}
            >
              {props.toolCall!.streamingOutput!.slice(-4000)}
            </pre>
          </Show>

          {/* Fallback: show raw output when done and no matched member */}
          <Show when={!matchedMember() && isDone() && props.toolCall?.output}>
            <pre
              class="whitespace-pre-wrap break-all"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: props.toolCall?.error ? 'var(--error)' : 'var(--text-tertiary)',
                'line-height': '1.6',
              }}
            >
              {props.toolCall!.error || props.toolCall!.output}
            </pre>
          </Show>

          {/* Empty state when no data yet */}
          <Show when={streamItems().length === 0 && !props.toolCall?.streamingOutput && !isDone()}>
            <div class="flex flex-col items-center justify-center py-16 gap-3">
              <Loader2
                class="animate-spin"
                style={{
                  width: '20px',
                  height: '20px',
                  color: 'color-mix(in srgb, var(--accent) 40%, transparent)',
                }}
              />
              <span
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '11px',
                  color: 'var(--text-muted)',
                }}
              >
                Waiting for agent activity...
              </span>
            </div>
          </Show>

          {/* Error display from member */}
          <Show when={matchedMember()?.error}>
            <div
              class="flex items-center gap-2 px-3 py-2 rounded-lg"
              style={{ background: 'var(--error-subtle)' }}
            >
              <XCircle
                class="flex-shrink-0"
                style={{ width: '12px', height: '12px', color: 'var(--error)' }}
              />
              <span
                style={{
                  'font-family': 'var(--font-ui), Geist, sans-serif',
                  'font-size': '12px',
                  color: 'var(--error)',
                }}
              >
                {matchedMember()!.error}
              </span>
            </div>
          </Show>

          {/* Result summary when done */}
          <Show when={matchedMember()?.result}>
            <div
              class="rounded-lg px-3 py-2"
              style={{
                background: 'var(--success-subtle)',
                border: '1px solid var(--success-border)',
              }}
            >
              <p
                style={{
                  'font-family': 'var(--font-ui), Geist, sans-serif',
                  'font-size': '13px',
                  color: 'var(--text-secondary)',
                  'line-height': '1.5',
                }}
              >
                {matchedMember()!.result}
              </p>
            </div>
          </Show>
        </div>
      </div>
    </div>
  )
}
