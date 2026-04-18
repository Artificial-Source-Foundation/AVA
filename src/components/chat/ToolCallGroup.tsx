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
 *
 * Improvements in Milestone 3:
 * - Better focus-visible indicators for keyboard navigation
 * - Improved indentation for nested tool calls (ml-8 with visual connector)
 * - Smoother expand/collapse transitions
 * - Clearer status badges with icons
 */

import { ChevronRight, Layers } from 'lucide-solid'
import { type Component, createEffect, createSignal, createUniqueId, For, Show } from 'solid-js'
import type { ToolCall } from '../../types'
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
  const [expanded, setExpanded] = createSignal(false)
  const contentId = createUniqueId()

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
    <div class="chat-tool-shell animate-tool-card-in overflow-hidden rounded-[10px] border border-[var(--border-default)] bg-[var(--tool-card-background)]">
      {/* Group header */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        aria-expanded={expanded()}
        aria-controls={contentId}
        class="tool-card-header flex h-10 cursor-pointer select-none items-center gap-2.5 px-3.5 transition-colors duration-[var(--duration-fast)] hover:bg-[var(--alpha-white-5)] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-inset"
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
          <span
            class="truncate"
            style={{
              'font-family': 'var(--font-ui), Geist, sans-serif',
              'font-size': '13px',
              color: 'var(--text-primary)',
            }}
          >
            {label()}
          </span>
          <Show when={activeSubtitle()}>
            <span
              class="truncate"
              style={{
                'font-size': '11px',
                color: 'var(--text-muted)',
                'margin-top': '2px',
              }}
            >
              {activeSubtitle()}
            </span>
          </Show>
        </div>

        {/* Count badge */}
        <span
          class="tabular-nums"
          style={{
            'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
            'font-size': '10px',
            'font-weight': '500',
            color: 'var(--text-tertiary)',
            background: 'var(--alpha-white-8)',
            padding: '2px 6px',
            'border-radius': '4px',
          }}
        >
          {props.group.calls.length}
        </span>

        {/* Duration */}
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

        {/* Chevron */}
        <ChevronRight
          class="flex-shrink-0 transition-transform duration-[var(--duration-fast)]"
          style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
          classList={{ 'rotate-90': expanded() }}
        />
      </div>

      {/* Expanded: individual cards */}
      <Show when={expanded()}>
        <div
          id={contentId}
          class="flex flex-col gap-1 border-t border-[var(--border-default)] px-2 pb-2 pt-1"
        >
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
  const anyPending = () => props.calls.some((c) => c.status === 'pending')
  const anyActive = () => props.calls.some((c) => c.status === 'running')
  const anyError = () => props.calls.some((c) => c.status === 'error')
  const allDone = () => props.calls.every((c) => c.status === 'success' || c.status === 'error')

  const [expanded, setExpanded] = createSignal(false)
  const contentId = createUniqueId()

  createEffect(() => {
    if (anyRunning()) {
      setExpanded(true)
    } else if (!props.isStreaming && allDone() && !anyError()) {
      setExpanded(false)
    }
  })

  const label = () => {
    if (anyActive()) return `Gathering context... (${props.calls.length})`
    if (anyPending()) return `Waiting for tools... (${props.calls.length})`
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

  // Progress stats for context gathering
  const progressStats = () => {
    const total = props.calls.length
    const done = props.calls.filter((c) => c.status === 'success').length
    const running = props.calls.filter((c) => c.status === 'running').length
    const pending = props.calls.filter((c) => c.status === 'pending').length
    const failed = props.calls.filter((c) => c.status === 'error').length
    return { done, running, pending, failed, total }
  }

  return (
    <div
      class="chat-tool-shell animate-tool-card-in overflow-hidden rounded-[10px]"
      style={{
        background: 'var(--tool-card-background)',
        border: anyRunning()
          ? '1px solid var(--tool-card-running-border)'
          : anyError()
            ? '1px solid var(--error-border)'
            : '1px solid var(--border-default)',
      }}
    >
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        aria-expanded={expanded()}
        aria-controls={contentId}
        class="tool-card-header flex cursor-pointer select-none items-center gap-2 px-3.5 transition-colors duration-[var(--duration-fast)] hover:bg-[var(--alpha-white-5)] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-inset"
        classList={{ 'tool-card-header--running': anyActive() }}
        style={{ height: '42px' }}
        onClick={() => setExpanded((v) => !v)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            setExpanded((v) => !v)
          }
        }}
      >
        {/* Layers icon - amber when active, muted when pending, gray when done */}
        <Layers
          class="flex-shrink-0"
          style={{
            width: '16px',
            height: '16px',
            color: anyActive()
              ? 'var(--warning)'
              : anyPending()
                ? 'var(--text-muted)'
                : 'var(--text-tertiary)',
            opacity: anyPending() ? 0.7 : 1,
          }}
        />

        <div class="flex flex-col min-w-0 flex-1">
          <span
            class="truncate"
            classList={{ 'tool-summary-shimmer tool-summary-shimmer--running': anyActive() }}
            style={{
              'font-family': 'var(--font-ui), Geist, sans-serif',
              'font-size': '13px',
              'font-weight': '500',
              color: 'var(--text-primary)',
              opacity: anyPending() ? 0.7 : 1,
            }}
          >
            {label()}
          </span>
          <Show when={activeSubtitle()}>
            <span
              class="truncate"
              style={{
                'font-size': '11px',
                color: 'var(--text-muted)',
                'margin-top': '2px',
              }}
            >
              {activeSubtitle()}
            </span>
          </Show>
        </div>

        {/* Active progress indicator - shows running count */}
        <Show when={anyActive()}>
          <span
            class="tabular-nums"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '10px',
              'font-weight': '500',
              color: 'var(--warning)',
              background: 'var(--warning-subtle)',
              padding: '2px 6px',
              'border-radius': '4px',
            }}
          >
            {progressStats().running} active
          </span>
        </Show>

        {/* Pending indicator */}
        <Show when={anyPending() && !anyActive()}>
          <span
            class="tabular-nums"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '10px',
              'font-weight': '500',
              color: 'var(--text-muted)',
              background: 'var(--alpha-white-8)',
              padding: '2px 6px',
              'border-radius': '4px',
            }}
          >
            {progressStats().pending} pending
          </span>
        </Show>

        {/* Error indicator */}
        <Show when={anyError() && !anyRunning()}>
          <span
            class="tabular-nums"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '10px',
              'font-weight': '500',
              color: 'var(--error)',
              background: 'var(--error-subtle)',
              padding: '2px 6px',
              'border-radius': '4px',
            }}
          >
            {progressStats().failed} failed
          </span>
        </Show>

        {/* Duration */}
        <Show when={totalDuration()}>
          <span
            class="tabular-nums whitespace-nowrap"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '11px',
              color: 'var(--text-muted)',
            }}
          >
            {totalDuration()}
          </span>
        </Show>

        <ChevronRight
          class="flex-shrink-0 transition-transform duration-[var(--duration-fast)]"
          style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
          classList={{ 'rotate-90': expanded() }}
        />
      </div>

      {/* Expanded: individual tool cards -- indented nested rows with visual connector */}
      <Show when={expanded()}>
        <div
          id={contentId}
          class="flex flex-col gap-1.5 border-t border-[var(--border-subtle)] pb-2 pt-2"
          style={{ 'padding-left': '44px', 'padding-right': '12px' }}
        >
          <div class="relative">
            {/* Visual connector line for nested tools */}
            <div
              class="absolute left-[-20px] top-0 bottom-0 w-px bg-[var(--border-subtle)]"
              aria-hidden="true"
            />
            <For each={props.calls}>{(tc) => <ToolCallCard toolCall={tc} />}</For>
          </div>
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
  const contentId = createUniqueId()

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
      <div class="chat-tool-shell my-1 overflow-hidden rounded-[10px] border border-[var(--border-default)] bg-[var(--tool-card-background)]">
        {/* Unified group header */}
        {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
        <div
          role="button"
          tabIndex={0}
          aria-expanded={expanded()}
          aria-controls={contentId}
          class="tool-card-header flex h-10 cursor-pointer select-none items-center gap-2.5 px-3.5 transition-colors hover:bg-[var(--alpha-white-5)] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-inset"
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
            <span
              class="truncate"
              style={{
                'font-family': 'var(--font-ui), Geist, sans-serif',
                'font-size': '13px',
                color: 'var(--text-primary)',
              }}
            >
              {anyRunning()
                ? `Running tools... (${toolSummary(props.toolCalls)})`
                : `Used ${props.toolCalls.length} tools (${toolSummary(props.toolCalls)})`}
            </span>
          </div>

          <span
            class="tabular-nums"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '10px',
              'font-weight': '500',
              color: 'var(--text-tertiary)',
              background: 'var(--alpha-white-8)',
              padding: '2px 6px',
              'border-radius': '4px',
            }}
          >
            {props.toolCalls.length}
          </span>

          <Show when={totalDuration()}>
            <span
              class="tabular-nums whitespace-nowrap"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: 'var(--text-muted)',
              }}
            >
              {totalDuration()}
            </span>
          </Show>

          <ChevronRight
            class="flex-shrink-0 transition-transform duration-[var(--duration-fast)]"
            style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
            classList={{ 'rotate-90': expanded() }}
          />
        </div>

        {/* Expanded: show per-type sub-groups */}
        <Show when={expanded()}>
          <div
            id={contentId}
            class="flex flex-col gap-1 border-t border-[var(--border-default)] px-2 pb-2 pt-1"
          >
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
