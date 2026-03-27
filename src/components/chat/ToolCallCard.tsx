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

import { Check, CheckCheck, ChevronRight, X } from 'lucide-solid'
import { type Component, createMemo, createSignal, Match, Show, Switch } from 'solid-js'
import { useSecondTicker } from '../../hooks/useElapsedTimer'
import { useSettings } from '../../stores/settings'
import type { ToolCall } from '../../types'
import { SubagentCard } from './SubagentCard'
import { ToolIcon } from './tool-call-icon'
import { ToolCallOutput } from './tool-call-output'
import { formatDuration, formatElapsed, getToolDescription } from './tool-call-utils'

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
      class="animate-tool-card-in rounded-[var(--radius-md)] border overflow-hidden transition-colors duration-[var(--duration-fast)]"
      classList={{
        'border-[var(--error)]/30': props.toolCall.status === 'error',
        'border-[var(--accent)]/30': isRunning(),
        'border-[var(--border-default)]': !isRunning() && props.toolCall.status !== 'error',
      }}
    >
      {/* Single-line header */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        aria-expanded={expanded()}
        class="flex items-center gap-2.5 px-3 py-2 text-[13px] cursor-pointer select-none hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)]"
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
        {/* Tool icon */}
        <ToolIcon name={props.toolCall.name} status={props.toolCall.status} />

        {/* Human-readable summary — shimmer while running */}
        <span
          class="truncate"
          classList={{
            'tool-summary-shimmer': isRunning(),
            'text-[var(--text-secondary)]': !isRunning(),
          }}
          title={summary()}
        >
          {summary()}
        </span>

        <span class="flex-1" />

        {/* Post-decision approval audit badge */}
        <Show when={props.toolCall.approvalDecision}>
          <ApprovalBadge decision={props.toolCall.approvalDecision!} />
        </Show>

        {/* Live elapsed / completed duration */}
        <Show when={duration() || (isRunning() && elapsed())}>
          <span class="text-[11px] text-[var(--text-muted)] tabular-nums whitespace-nowrap">
            {duration() ?? elapsed()}
          </span>
        </Show>

        {/* Expand chevron */}
        <Show when={hasOutput()}>
          <ChevronRight
            class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]"
            classList={{ 'rotate-90': expanded() }}
          />
        </Show>
      </div>

      {/* Live streaming output while running */}
      <Show when={hasStreamingOutput()}>
        <div class="px-3 pb-2 border-t border-[var(--border-subtle)]">
          <pre class="scroll-fade-mask text-[11px] text-[var(--text-muted)] font-mono whitespace-pre-wrap break-all max-h-32 overflow-y-auto scrollbar-none leading-relaxed mt-1.5">
            {props.toolCall.streamingOutput!.slice(-2000)}
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
