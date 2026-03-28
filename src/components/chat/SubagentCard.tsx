/**
 * Subagent Card
 *
 * Matches Pencil "Tool States" design:
 *
 * Running: rounded-10, fill #0F0F12, border #5E5CE630
 *   Header: 44px, fill #5E5CE608, bot icon (16px #5E5CE6),
 *           two-line info (name: Geist 13px 500 #F5F5F7, sub: Geist Mono 10px #48484A),
 *           spinner (12px #5E5CE6), elapsed (Geist Mono 11px #5E5CE6), chevron
 *   Body: bg #0A0A0C, padding 10px 14px, step checklist with check/loader icons
 *
 * Done: collapsed 44px row, green bot icon (16px #34C759),
 *       two-line info (#86868B name, #48484A sub),
 *       check (12px #34C759), duration (#48484A), chevron-right
 *
 * Background Worker (HQ): amber border #F5A62330, hard-hat icon #F5A623
 */

import {
  Bot,
  Check,
  ChevronDown,
  ChevronRight,
  HardHat,
  Loader2,
  Octagon,
  XCircle,
} from 'lucide-solid'

/** Stub for abortExecutor (replaces @ava/core-v2/agent import) */
function abortExecutor(_id: string): boolean {
  return false
}

import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSecondTicker } from '../../hooks/useElapsedTimer'
import { useLayout } from '../../stores/layout'
import { useTeam } from '../../stores/team'
import type { ToolCall } from '../../types'
import { formatDuration, formatElapsed } from './tool-call-utils'

interface SubagentCardProps {
  toolCall: ToolCall
}

interface NestedToolCall {
  name: string
  summary: string
  status: 'success' | 'error' | 'running'
}

function parseNestedToolCalls(output: string): NestedToolCall[] {
  const calls: NestedToolCall[] = []
  const lines = output.split('\n')
  for (const line of lines) {
    const successMatch = line.match(/[✓✔☑]\s*(\w+)\s*(.*)/)
    const errorMatch = line.match(/[✗✘☒]\s*(\w+)\s*(.*)/)
    const runningMatch = line.match(/[⟳↻…]\s*(\w+)\s*(.*)/)
    if (successMatch) {
      calls.push({ name: successMatch[1], summary: successMatch[2].trim(), status: 'success' })
    } else if (errorMatch) {
      calls.push({ name: errorMatch[1], summary: errorMatch[2].trim(), status: 'error' })
    } else if (runningMatch) {
      calls.push({ name: runningMatch[1], summary: runningMatch[2].trim(), status: 'running' })
    }
  }
  return calls
}

/** Detect if this is a background HQ worker */
function isHqWorker(toolCall: ToolCall): boolean {
  const args = toolCall.args as Record<string, unknown>
  return !!(args.source === 'hq' || args.hq || args.background)
}

export const SubagentCard: Component<SubagentCardProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)
  const team = useTeam()
  const { openSubagentDetail } = useLayout()

  const isRunning = () => props.toolCall.status === 'running' || props.toolCall.status === 'pending'
  const isError = () => props.toolCall.status === 'error'
  const isSuccess = () => props.toolCall.status === 'success'
  const isDone = () => isSuccess() || isError()
  const hasOutput = () => !!(props.toolCall.output || props.toolCall.error)
  const isHq = () => isHqWorker(props.toolCall)
  const nowTick = useSecondTicker(isRunning)

  const goal = () => {
    const args = props.toolCall.args
    const g = String(args.goal ?? args.description ?? args.prompt ?? '')
    return g.length > 80 ? `${g.slice(0, 77)}...` : g || 'subagent task'
  }

  /** Agent name label (e.g., "Scout -- Exploring codebase structure") */
  const agentName = () => {
    const args = props.toolCall.args as Record<string, unknown>
    const role = String(args.role ?? args.agent_type ?? 'Scout')
    const taskLabel = goal()
    return `${role} -- ${taskLabel}`
  }

  /** Subtitle (e.g., "haiku . read-only . 3 tools used") */
  const agentSubtitle = () => {
    const args = props.toolCall.args as Record<string, unknown>
    const parts: string[] = []
    if (args.model) parts.push(String(args.model))
    if (args.mode) parts.push(String(args.mode))
    return parts.length > 0 ? parts.join(' \u00b7 ') : ''
  }

  const matchedMember = createMemo(() => {
    const args = props.toolCall.args
    const taskText = String(args.task ?? args.prompt ?? args.goal ?? '')
    if (!taskText) return null
    for (const member of team.allMembers()) {
      if (member.task && taskText.startsWith(member.task.slice(0, 40))) {
        return member
      }
    }
    return null
  })

  const duration = () => {
    if (!props.toolCall.completedAt) return null
    return formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
  }

  const nestedCalls = createMemo(() => {
    if (!props.toolCall.output) return []
    return parseNestedToolCalls(props.toolCall.output)
  })

  const elapsed = createMemo(() => {
    if (!isRunning()) return ''
    nowTick()
    return formatElapsed(props.toolCall.startedAt)
  })

  const handleStop = (e: Event) => {
    e.stopPropagation()
    const member = matchedMember()
    if (member) {
      const aborted = abortExecutor(member.id)
      if (aborted) {
        team.updateMemberStatus(member.id, 'error')
        team.updateMember(member.id, { error: 'Stopped by user' })
      }
    }
  }

  /** Color scheme based on agent type */
  const accentColor = () => (isHq() ? 'var(--warning)' : 'var(--thinking-accent)')
  const borderColor = () => {
    if (isError()) return 'var(--error-border)'
    if (isRunning()) return isHq() ? 'var(--warning-border)' : 'var(--thinking-border)'
    return 'var(--border-default)'
  }

  return (
    <div
      class="chat-tool-shell animate-tool-card-in overflow-hidden rounded-[10px] transition-colors duration-[var(--duration-fast)]"
      style={{
        background: 'var(--tool-card-background)',
        border: `1px solid ${borderColor()}`,
      }}
    >
      {/* Header -- 44px */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button crash in WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        aria-expanded={expanded()}
        class="flex cursor-pointer select-none items-center justify-between px-3.5 transition-colors duration-[var(--duration-fast)] hover:bg-[var(--alpha-white-5)]"
        style={{
          height: '44px',
          background: isRunning()
            ? isHq()
              ? 'var(--warning-subtle)'
              : 'var(--thinking-subtle)'
            : 'transparent',
        }}
        onClick={() => {
          openSubagentDetail(props.toolCall.id)
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            openSubagentDetail(props.toolCall.id)
          }
        }}
      >
        {/* Left: icon + two-line info */}
        <div class="flex items-center gap-2.5 min-w-0 flex-1">
          {/* Bot / HardHat icon */}
          <Show
            when={!isHq()}
            fallback={
              <HardHat
                class="flex-shrink-0"
                style={{
                  width: '16px',
                  height: '16px',
                  color: isDone()
                    ? isSuccess()
                      ? 'var(--success)'
                      : 'var(--error)'
                    : 'var(--warning)',
                }}
              />
            }
          >
            <Bot
              class="flex-shrink-0"
              style={{
                width: '16px',
                height: '16px',
                color: isDone()
                  ? isSuccess()
                    ? 'var(--success)'
                    : 'var(--error)'
                  : 'var(--thinking-accent)',
              }}
            />
          </Show>

          {/* Two-line info */}
          <div class="flex flex-col gap-px min-w-0 flex-1">
            <span
              class="truncate"
              style={{
                'font-family': 'var(--font-ui), Geist, sans-serif',
                'font-size': '13px',
                'font-weight': '500',
                color: isDone() ? 'var(--text-tertiary)' : 'var(--text-primary)',
              }}
              title={goal()}
            >
              {agentName()}
            </span>
            <Show when={agentSubtitle()}>
              <span
                class="truncate"
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '10px',
                  color: 'var(--text-muted)',
                }}
              >
                {agentSubtitle()}
              </span>
            </Show>
          </div>
        </div>

        {/* Right: status indicators */}
        <div class="flex items-center gap-2 flex-shrink-0">
          {/* Stop button */}
          <Show when={isRunning() && matchedMember()}>
            <button
              type="button"
              onClick={handleStop}
              class="rounded p-1 text-[var(--text-muted)] transition-colors hover:bg-[var(--error-subtle)] hover:text-[var(--error)]"
              title="Stop agent"
              aria-label="Stop agent"
            >
              <Octagon style={{ width: '12px', height: '12px' }} />
            </button>
          </Show>

          {/* Running: spinner + elapsed */}
          <Show when={isRunning()}>
            <Loader2
              class="flex-shrink-0 animate-spin"
              style={{ width: '12px', height: '12px', color: accentColor() }}
            />
            <span
              class="tabular-nums whitespace-nowrap"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: accentColor(),
              }}
            >
              {elapsed()}
            </span>
          </Show>

          {/* Done: check + duration */}
          <Show when={isDone()}>
            <Show when={isSuccess()}>
              <Check
                class="flex-shrink-0"
                style={{ width: '12px', height: '12px', color: 'var(--success)' }}
              />
            </Show>
            <Show when={isError()}>
              <XCircle
                class="flex-shrink-0"
                style={{ width: '12px', height: '12px', color: 'var(--error)' }}
              />
            </Show>
            <Show when={duration()}>
              <span
                class="tabular-nums whitespace-nowrap"
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '11px',
                  color: 'var(--text-muted)',
                }}
              >
                {duration()}
              </span>
            </Show>
          </Show>

          {/* Chevron — toggles inline expand; stops propagation to avoid opening detail view */}
          <Show when={hasOutput()}>
            {/* biome-ignore lint/a11y/useSemanticElements: chevron toggle uses div+role=button to avoid nested button */}
            <div
              role="button"
              tabIndex={-1}
              class="rounded p-0.5 transition-colors hover:bg-[var(--alpha-white-8)]"
              onClick={(e) => {
                e.stopPropagation()
                setExpanded((v) => !v)
              }}
              onKeyDown={(e) => {
                if (e.key === 'Enter' || e.key === ' ') {
                  e.preventDefault()
                  e.stopPropagation()
                  setExpanded((v) => !v)
                }
              }}
            >
              <Show
                when={expanded()}
                fallback={
                  <ChevronRight
                    class="flex-shrink-0"
                    style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
                  />
                }
              >
                <ChevronDown
                  class="flex-shrink-0"
                  style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
                />
              </Show>
            </div>
          </Show>
        </div>
      </div>

      {/* Streaming output */}
      <Show when={isRunning() && props.toolCall.streamingOutput}>
        <div
          style={{
            background: 'var(--background)',
            padding: '10px 14px',
          }}
        >
          <pre
            class="whitespace-pre-wrap break-all max-h-32 overflow-y-auto scrollbar-none"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '11px',
              color: 'var(--text-tertiary)',
              'line-height': '1.6',
            }}
          >
            {props.toolCall.streamingOutput!.slice(-2000)}
          </pre>
        </div>
      </Show>

      {/* Expanded body -- step checklist */}
      <Show when={expanded() && hasOutput()}>
        <div
          style={{
            background: 'var(--background)',
            padding: '10px 14px',
          }}
        >
          {/* Nested tool calls as step checklist */}
          <Show when={nestedCalls().length > 0}>
            <div class="flex flex-col" style={{ gap: '6px' }}>
              <For each={nestedCalls()}>
                {(call) => (
                  <div class="flex items-center gap-2">
                    {/* Step icon */}
                    <Show
                      when={call.status === 'success'}
                      fallback={
                        <Show
                          when={call.status === 'running'}
                          fallback={
                            <XCircle
                              class="flex-shrink-0"
                              style={{ width: '12px', height: '12px', color: 'var(--error)' }}
                            />
                          }
                        >
                          <Loader2
                            class="flex-shrink-0 animate-spin"
                            style={{ width: '12px', height: '12px', color: accentColor() }}
                          />
                        </Show>
                      }
                    >
                      <Check
                        class="flex-shrink-0"
                        style={{ width: '12px', height: '12px', color: 'var(--success)' }}
                      />
                    </Show>
                    <span
                      style={{
                        'font-family': 'var(--font-ui), Geist, sans-serif',
                        'font-size': '12px',
                        color:
                          call.status === 'running'
                            ? 'var(--text-primary)'
                            : 'var(--text-tertiary)',
                      }}
                    >
                      {call.summary || call.name}
                    </span>
                  </div>
                )}
              </For>
            </div>
          </Show>

          {/* Raw output fallback */}
          <Show
            when={nestedCalls().length === 0 && (props.toolCall.output || props.toolCall.error)}
          >
            <pre
              class="whitespace-pre-wrap break-all max-h-48 overflow-y-auto scrollbar-none"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: props.toolCall.error ? 'var(--error)' : 'var(--text-tertiary)',
                'line-height': '1.6',
              }}
            >
              {props.toolCall.error || props.toolCall.output}
            </pre>
          </Show>
        </div>
      </Show>
    </div>
  )
}
