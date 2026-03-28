/**
 * Tool Call Card
 *
 * Expandable inline card showing a single tool call with:
 * - Tool-specific icon (spinning when running, red when error)
 * - Human-readable action summary
 * - Live elapsed timer while running
 * - Rich expanded output (syntax-highlighted, diff view, error categorization)
 * - Post-decision approval audit badge (Approved / Auto-approved / Denied)
 */

import { Check, CheckCheck, ChevronRight, Loader2, X } from 'lucide-solid'
import { type Component, createMemo, createSignal, Match, Show, Switch } from 'solid-js'
import { useSecondTicker } from '../../hooks/useElapsedTimer'
import { useSettings } from '../../stores/settings'
import type { ToolCall } from '../../types'
import { SubagentCard } from './SubagentCard'
import { ToolIcon } from './tool-call-icon'
import { ToolCallOutput } from './tool-call-output'
import { formatDuration, formatElapsed, getToolDescription } from './tool-call-utils'

const TOOL_CARD_STYLE = {
  idleBorder: '1px solid var(--border-subtle)',
  expandedBackground: 'var(--tool-card-background)',
  runningBorder: '1px solid var(--tool-card-running-border)',
  runningShadow: 'var(--tool-card-running-shadow)',
  errorBorder: '1px solid var(--error-border)',
} as const

// ============================================================================
// Approval Audit Badge
// ============================================================================

/**
 * Small inline badge shown in the tool card header after the user acts on
 * the ApprovalDock. Lets users scroll back and see what they approved.
 */
const ApprovalBadge: Component<{ decision: 'once' | 'always' | 'denied' }> = (props) => {
  return (
    <Switch>
      <Match when={props.decision === 'denied'}>
        <span class="inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded-full text-[10px] font-medium bg-[var(--error)]/10 text-[var(--error)] border border-[var(--error)]/30 flex-shrink-0">
          <X class="w-2.5 h-2.5" />
          Denied
        </span>
      </Match>
      <Match when={props.decision === 'always'}>
        <span class="inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded-full text-[10px] font-medium bg-[var(--success)]/10 text-[var(--success)] border border-[var(--success)]/30 flex-shrink-0">
          <CheckCheck class="w-2.5 h-2.5" />
          Auto-approved
        </span>
      </Match>
      <Match when={true}>
        <span class="inline-flex items-center gap-0.5 px-1.5 py-0.5 rounded-full text-[10px] font-medium bg-[var(--success)]/10 text-[var(--success)] border border-[var(--success)]/30 flex-shrink-0">
          <Check class="w-2.5 h-2.5" />
          Approved
        </span>
      </Match>
    </Switch>
  )
}

const ToolCallCardContent: Component<ToolCallCardProps> = (props) => {
  const { settings } = useSettings()
  // When toolResponseStyle is 'detailed', tool results start expanded by default
  const defaultExpanded = () => settings().ui.toolResponseStyle === 'detailed'
  const [expanded, setExpanded] = createSignal(defaultExpanded())

  const summary = () => getToolDescription(props.toolCall.name, props.toolCall.args)
  const isRunning = () => props.toolCall.status === 'running' || props.toolCall.status === 'pending'
  const hasOutput = () => !!(props.toolCall.output || props.toolCall.error || props.toolCall.diff)
  const hasStreamingOutput = () => isRunning() && !!props.toolCall.streamingOutput
  const nowTick = useSecondTicker(isRunning)

  const duration = () => {
    if (!props.toolCall.completedAt) return null
    return formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
  }
  const elapsed = createMemo(() => {
    if (!isRunning()) return ''
    nowTick()
    return formatElapsed(props.toolCall.startedAt)
  })

  return (
    <div
      class="chat-tool-shell animate-tool-card-in rounded-[10px] overflow-hidden transition-colors duration-[var(--duration-fast)]"
      style={{
        background: expanded() ? TOOL_CARD_STYLE.expandedBackground : 'transparent',
        border: isRunning()
          ? TOOL_CARD_STYLE.runningBorder
          : props.toolCall.status === 'error'
            ? TOOL_CARD_STYLE.errorBorder
            : TOOL_CARD_STYLE.idleBorder,
        ...(isRunning() ? { 'box-shadow': TOOL_CARD_STYLE.runningShadow } : {}),
      }}
    >
      {/* Single-line header — 40px height, bottom border when collapsed */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        aria-expanded={expanded()}
        class="tool-card-header flex h-10 cursor-pointer select-none items-center gap-2.5 px-3 text-[13px] transition-colors duration-[var(--duration-fast)] hover:bg-[var(--alpha-white-5)]"
        classList={{
          'border-b border-[var(--border-subtle)]': !expanded(),
          'bg-[var(--alpha-white-5)]': expanded(),
        }}
        onClick={() => {
          if (hasOutput()) setExpanded((v) => !v)
        }}
        onKeyDown={(e) => {
          if ((e.key === 'Enter' || e.key === ' ') && hasOutput()) {
            e.preventDefault()
            setExpanded((v) => !v)
          }
        }}
      >
        {/* Tool icon — 16px, color-coded by type */}
        <ToolIcon name={props.toolCall.name} status={props.toolCall.status} />

        {/* Tool name / summary */}
        <span
          class="truncate text-[13px] text-[var(--text-primary)]"
          style={isRunning() ? { opacity: '0.9' } : undefined}
          title={summary()}
        >
          {summary()}
        </span>

        <span class="flex-1" />

        {/* Post-decision approval audit badge */}
        <Show when={props.toolCall.approvalDecision}>
          <ApprovalBadge decision={props.toolCall.approvalDecision!} />
        </Show>

        {/* Status badge for expanded cards */}
        <Show when={expanded() && !isRunning()}>
          <span
            class="text-[10px] font-medium px-1.5 py-0.5 rounded-full"
            classList={{
              'text-[var(--success)] bg-[var(--success-subtle)]':
                props.toolCall.status === 'success',
              'text-[var(--error)] bg-[var(--error-subtle)]': props.toolCall.status === 'error',
            }}
          >
            {props.toolCall.status === 'error' ? 'Error' : 'Done'}
          </span>
        </Show>

        {/* Running indicator */}
        <Show when={isRunning()}>
          <Loader2
            class="animate-spin flex-shrink-0"
            style={{ width: '14px', height: '14px', color: 'var(--accent)' }}
          />
          <span
            class="tabular-nums whitespace-nowrap"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '11px',
              color: 'var(--accent)',
            }}
          >
            {elapsed()}
          </span>
        </Show>

        {/* Completed duration badge */}
        <Show when={!isRunning() && duration()}>
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

        {/* Expand chevron */}
        <Show when={hasOutput()}>
          <ChevronRight
            class="flex-shrink-0 transition-transform duration-[var(--duration-fast)]"
            style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
            classList={{ 'rotate-90': expanded() }}
          />
        </Show>
      </div>

      {/* Live streaming output while running */}
      <Show when={hasStreamingOutput()}>
        <div class="border-t border-[var(--border-subtle)] px-3 pb-2">
          <pre class="scroll-fade-mask mt-1.5 max-h-32 overflow-y-auto whitespace-pre-wrap break-all font-[var(--font-ui-mono)] text-[11px] leading-relaxed text-[var(--text-muted)] scrollbar-none">
            {props.toolCall.streamingOutput!.slice(-2000)}
            <span class="ml-px inline-block h-[14px] w-[6px] animate-pulse align-middle bg-[var(--chat-streaming-indicator)]" />
          </pre>
        </div>
      </Show>

      {/* Expanded output — smooth height reveal via CSS grid row trick.
          Content is always mounted when hasOutput() so the grid animation
          plays fully in both directions; overflow:hidden clips it. */}
      <Show when={hasOutput()}>
        <div class="tool-card-body-grid" data-expanded={expanded() ? 'true' : 'false'}>
          <div class="tool-card-body-inner">
            <ToolCallOutput toolCall={props.toolCall} />
          </div>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// Component
// ============================================================================

interface ToolCallCardProps {
  toolCall: ToolCall
}

function isDelegationTool(name: string): boolean {
  return name === 'task' || name.startsWith('delegate_')
}

export const ToolCallCard: Component<ToolCallCardProps> = (props) => {
  const delegated = createMemo(() => isDelegationTool(props.toolCall.name))

  return (
    <Show when={!delegated()} fallback={<SubagentCard toolCall={props.toolCall} />}>
      <ToolCallCardContent toolCall={props.toolCall} />
    </Show>
  )
}
