/**
 * Tool Call Group
 *
 * Claude Code-style grouping of consecutive same-type tool calls.
 * Single calls render directly; multi-call groups get a collapsible header
 * with count badge, total duration, and auto-expand/collapse behavior.
 *
 * Layout (multi-item group):
 *   [spinner] Reading 3 files...              [2.3s] [v]
 *     └ src/components/chat/ToolCallCard.tsx
 *
 *   [check]  Read 3 files                    [4.1s] [v]   ← collapsed
 */

import { ChevronRight } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, Show } from 'solid-js'
import type { ToolCall, ToolCallStatus } from '../../types'
import { ToolCallCard } from './ToolCallCard'
import { ToolIcon } from './tool-call-icon'
import {
  type ContextSegment,
  describeContextGroup,
  formatDuration,
  getGroupDuration,
  getGroupLabel,
  getGroupStatus,
  getToolDescription,
  groupToolCalls,
  type ToolCallGroupData,
} from './tool-call-utils'

// ============================================================================
// Multi-item group header
// ============================================================================

interface GroupHeaderProps {
  group: ToolCallGroupData
  isStreaming?: boolean
}

const GroupHeader: Component<GroupHeaderProps> = (props) => {
  const [expanded, setExpanded] = createSignal(props.group.isActive)

  // Auto-expand when active, auto-collapse when complete (but not while streaming)
  createEffect(() => {
    if (props.group.isActive) {
      setExpanded(true)
    } else if (
      !props.isStreaming &&
      !props.group.isActive &&
      props.group.calls.every((c) => c.status === 'success')
    ) {
      setExpanded(false)
    }
  })

  const status = () => getGroupStatus(props.group)
  const label = () => getGroupLabel(props.group)
  const duration = () => {
    const ms = getGroupDuration(props.group)
    return ms !== null ? formatDuration(ms) : null
  }

  // Show the currently-running call's description as a subtitle
  const activeSubtitle = () => {
    if (!props.group.isActive) return null
    const running = props.group.calls.find((c) => c.status === 'running')
    if (!running) return null
    return getToolDescription(running.name, running.args)
  }

  return (
    <div class="animate-tool-card-in rounded-[var(--radius-md)] border border-[var(--border-subtle)] overflow-hidden">
      {/* Group header */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        class="flex items-center gap-2.5 px-3 py-2 text-[13px] cursor-pointer select-none hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)]"
        onClick={() => setExpanded((v) => !v)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            setExpanded((v) => !v)
          }
        }}
      >
        <ToolIcon name={props.group.toolName} status={status()} />

        <div class="flex flex-col min-w-0 flex-1">
          <span class="text-[var(--text-secondary)] truncate">{label()}</span>
          <Show when={activeSubtitle()}>
            <span class="text-[11px] text-[var(--text-muted)] truncate mt-0.5">
              {activeSubtitle()}
            </span>
          </Show>
        </div>

        {/* Count badge */}
        <span class="text-[10px] font-medium text-[var(--text-muted)] bg-[var(--alpha-white-5)] px-1.5 py-0.5 rounded-[var(--radius-sm)] tabular-nums">
          {props.group.calls.length}
        </span>

        {/* Duration */}
        <Show when={duration()}>
          <span class="text-[11px] text-[var(--text-muted)] tabular-nums whitespace-nowrap">
            {duration()}
          </span>
        </Show>

        {/* Chevron */}
        <ChevronRight
          class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]"
          classList={{ 'rotate-90': expanded() }}
        />
      </div>

      {/* Expanded: individual cards */}
      <Show when={expanded()}>
        <div class="px-2 pb-2 pt-1 flex flex-col gap-1 border-t border-[var(--border-subtle)]">
          <For each={props.group.calls}>{(tc) => <ToolCallCard toolCall={tc} />}</For>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// Main component
// ============================================================================

interface ToolCallGroupProps {
  toolCalls: ToolCall[]
  isStreaming?: boolean
}

// ============================================================================
// Context group (consecutive read/glob/grep → "Gathering context...")
// ============================================================================

interface ContextGroupHeaderProps {
  calls: ToolCall[]
  isStreaming?: boolean
}

/**
 * Collapsible header for a run of consecutive context-gathering tool calls.
 * Shows "Gathering context... (N)" while active and "Gathered context (3 files read, 2 searches)"
 * when complete. Individual tool cards are viewable by expanding.
 */
export const ContextGroupHeader: Component<ContextGroupHeaderProps> = (props) => {
  const anyRunning = () => props.calls.some((c) => c.status === 'running' || c.status === 'pending')
  const anyError = () => props.calls.some((c) => c.status === 'error')
  const allDone = () => props.calls.every((c) => c.status === 'success' || c.status === 'error')

  const [expanded, setExpanded] = createSignal(anyRunning())

  createEffect(() => {
    if (anyRunning()) {
      setExpanded(true)
    } else if (!props.isStreaming && allDone() && !anyError()) {
      setExpanded(false)
    }
  })

  const status = (): ToolCallStatus => (anyRunning() ? 'running' : anyError() ? 'error' : 'success')

  const label = () => {
    if (anyRunning()) return `Gathering context... (${props.calls.length})`
    return describeContextGroup(props.calls)
  }

  // Show the currently-running tool's description as subtitle
  const activeSubtitle = () => {
    if (!anyRunning()) return null
    const running = props.calls.find((c) => c.status === 'running')
    if (!running) return null
    return getToolDescription(running.name, running.args)
  }

  const totalDuration = () => {
    const completed = props.calls.filter((c) => c.completedAt && c.startedAt)
    if (completed.length === 0) return null
    const total = completed.reduce((sum, c) => sum + (c.completedAt! - c.startedAt), 0)
    return formatDuration(total)
  }

  return (
    <div
      class="animate-tool-card-in rounded-[var(--radius-md)] border overflow-hidden"
      classList={{
        'border-[var(--error)]/30': anyError(),
        'border-[var(--accent)]/30': anyRunning(),
        'border-[var(--border-subtle)]': !anyRunning() && !anyError(),
      }}
    >
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        class="flex items-center gap-2.5 px-3 py-2 text-[13px] cursor-pointer select-none hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)]"
        onClick={() => setExpanded((v) => !v)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            setExpanded((v) => !v)
          }
        }}
      >
        <ToolIcon name="read" status={status()} />

        <div class="flex flex-col min-w-0 flex-1">
          <span class="text-[var(--text-secondary)] truncate">{label()}</span>
          <Show when={activeSubtitle()}>
            <span class="text-[11px] text-[var(--text-muted)] truncate mt-0.5">
              {activeSubtitle()}
            </span>
          </Show>
        </div>

        {/* Count badge */}
        <span class="text-[10px] font-medium text-[var(--text-muted)] bg-[var(--alpha-white-5)] px-1.5 py-0.5 rounded-[var(--radius-sm)] tabular-nums">
          {props.calls.length}
        </span>

        {/* Duration */}
        <Show when={totalDuration()}>
          <span class="text-[11px] text-[var(--text-muted)] tabular-nums whitespace-nowrap">
            {totalDuration()}
          </span>
        </Show>

        <ChevronRight
          class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]"
          classList={{ 'rotate-90': expanded() }}
        />
      </div>

      {/* Expanded: individual tool cards */}
      <Show when={expanded()}>
        <div class="px-2 pb-2 pt-1 flex flex-col gap-1 border-t border-[var(--border-subtle)]">
          <For each={props.calls}>{(tc) => <ToolCallCard toolCall={tc} />}</For>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// ContextAwareToolList — renders context-segmented tool calls
// ============================================================================

interface ContextAwareToolListProps {
  segments: ContextSegment[]
  isStreaming?: boolean
}

/**
 * Renders a list of tool calls with context grouping applied.
 * Consecutive context tools (read/glob/grep) are shown as a single collapsible group;
 * non-context tools (write, edit, bash) always render individually.
 */
export const ContextAwareToolList: Component<ContextAwareToolListProps> = (props) => {
  return (
    <For each={props.segments}>
      {(seg) => (
        <Show
          when={
            seg.kind === 'context' && (seg as ContextSegment & { kind: 'context' }).calls.length > 1
          }
          fallback={
            <Show
              when={seg.kind === 'context'}
              fallback={
                <ToolCallCard toolCall={(seg as ContextSegment & { kind: 'single' }).call} />
              }
            >
              <ToolCallCard toolCall={(seg as ContextSegment & { kind: 'context' }).calls[0]} />
            </Show>
          }
        >
          <ContextGroupHeader
            calls={(seg as ContextSegment & { kind: 'context' }).calls}
            isStreaming={props.isStreaming}
          />
        </Show>
      )}
    </For>
  )
}

/** Build a summary like "bash 3, read 2, glob 1" */
function toolSummary(calls: ToolCall[]): string {
  const counts = new Map<string, number>()
  for (const c of calls) counts.set(c.name, (counts.get(c.name) ?? 0) + 1)
  return Array.from(counts.entries())
    .map(([name, count]) => (count > 1 ? `${name} ${count}` : name))
    .join(', ')
}

export const ToolCallGroup: Component<ToolCallGroupProps> = (props) => {
  const groups = () => groupToolCalls(props.toolCalls)
  const [expanded, setExpanded] = createSignal(false)

  // Auto-expand while streaming (tools actively running), collapse when done
  createEffect(() => {
    const anyRunning = props.toolCalls.some((c) => c.status === 'running' || c.status === 'pending')
    if (anyRunning) setExpanded(true)
    else if (
      !props.isStreaming &&
      props.toolCalls.length > 0 &&
      props.toolCalls.every((c) => c.status === 'success' || c.status === 'error')
    ) {
      setExpanded(false)
    }
  })

  const totalDuration = () => {
    let total = 0
    for (const c of props.toolCalls) {
      if (c.startedAt && c.completedAt) total += c.completedAt - c.startedAt
    }
    return total > 0 ? formatDuration(total) : null
  }

  const anyError = () => props.toolCalls.some((c) => c.status === 'error')
  const anyRunning = () => props.toolCalls.some((c) => c.status === 'running')

  // For 1-2 tool calls, render inline without the mega-group wrapper
  const shouldGroup = () => props.toolCalls.length > 2

  return (
    <Show
      when={shouldGroup()}
      fallback={
        <div class="flex flex-col gap-1.5 my-1">
          <For each={groups()}>
            {(group) => (
              <Show
                when={group.calls.length > 1}
                fallback={<ToolCallCard toolCall={group.calls[0]} />}
              >
                <GroupHeader group={group} isStreaming={props.isStreaming} />
              </Show>
            )}
          </For>
        </div>
      }
    >
      <div class="my-1 rounded-[var(--radius-md)] border border-[var(--border-subtle)] overflow-hidden">
        {/* Unified group header */}
        {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
        <div
          role="button"
          tabIndex={0}
          class="flex items-center gap-2.5 px-3 py-2 text-[13px] cursor-pointer select-none hover:bg-[var(--alpha-white-3)] transition-colors"
          onClick={() => setExpanded((v) => !v)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') {
              e.preventDefault()
              setExpanded((v) => !v)
            }
          }}
        >
          <ToolIcon
            name={anyRunning() ? 'bash' : 'read'}
            status={anyRunning() ? 'running' : anyError() ? 'error' : 'success'}
          />

          <div class="flex flex-col min-w-0 flex-1">
            <span class="text-[var(--text-secondary)] truncate">
              {anyRunning()
                ? `Running tools... (${toolSummary(props.toolCalls)})`
                : `Used ${props.toolCalls.length} tools (${toolSummary(props.toolCalls)})`}
            </span>
          </div>

          <span class="text-[10px] font-medium text-[var(--text-muted)] bg-[var(--alpha-white-5)] px-1.5 py-0.5 rounded-[var(--radius-sm)] tabular-nums">
            {props.toolCalls.length}
          </span>

          <Show when={totalDuration()}>
            <span class="text-[11px] text-[var(--text-muted)] tabular-nums whitespace-nowrap">
              {totalDuration()}
            </span>
          </Show>

          <ChevronRight
            class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]"
            classList={{ 'rotate-90': expanded() }}
          />
        </div>

        {/* Expanded: show per-type sub-groups */}
        <Show when={expanded()}>
          <div class="px-2 pb-2 pt-1 flex flex-col gap-1 border-t border-[var(--border-subtle)]">
            <For each={groups()}>
              {(group) => (
                <Show
                  when={group.calls.length > 1}
                  fallback={<ToolCallCard toolCall={group.calls[0]} />}
                >
                  <GroupHeader group={group} isStreaming={props.isStreaming} />
                </Show>
              )}
            </For>
          </div>
        </Show>
      </div>
    </Show>
  )
}
