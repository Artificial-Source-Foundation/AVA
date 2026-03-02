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
import type { ToolCall } from '../../types'
import { ToolCallCard } from './ToolCallCard'
import { ToolIcon } from './tool-call-icon'
import {
  formatDuration,
  getGroupDuration,
  getGroupLabel,
  getGroupStatus,
  groupToolCalls,
  summarizeAction,
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

  // Show the currently-running call's file path as a subtitle
  const activeSubtitle = () => {
    if (!props.group.isActive) return null
    const running = props.group.calls.find((c) => c.status === 'running')
    if (!running) return null
    return summarizeAction(running.name, running.args)
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

export const ToolCallGroup: Component<ToolCallGroupProps> = (props) => {
  const groups = () => groupToolCalls(props.toolCalls)

  return (
    <div class="flex flex-col gap-1.5 my-1">
      <For each={groups()}>
        {(group) => (
          <Show when={group.calls.length > 1} fallback={<ToolCallCard toolCall={group.calls[0]} />}>
            <GroupHeader group={group} isStreaming={props.isStreaming} />
          </Show>
        )}
      </For>
    </div>
  )
}
