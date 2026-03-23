/**
 * Inline Plan Card Component
 *
 * Read-only version of PlanCard for rendering plans stored in message metadata.
 * Shown in chat history when a message contains plan data (metadata.plan).
 * No interactive approve/reject/edit buttons - the plan has already been resolved.
 */

import { Check, ChevronDown, ChevronUp, ClipboardList } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { PlanData, PlanStepAction } from '../../types/rust-ipc'

const ACTION_COLORS: Record<PlanStepAction, { bg: string; text: string; label: string }> = {
  research: { bg: 'rgba(6, 182, 212, 0.12)', text: '#06B6D4', label: 'Research' },
  implement: { bg: 'rgba(59, 130, 246, 0.12)', text: '#3B82F6', label: 'Implement' },
  test: { bg: 'rgba(34, 197, 94, 0.12)', text: '#22C55E', label: 'Test' },
  review: { bg: 'rgba(245, 158, 11, 0.12)', text: '#F59E0B', label: 'Review' },
}

export interface InlinePlanCardProps {
  plan: PlanData
  onViewFull?: () => void
}

export const InlinePlanCard: Component<InlinePlanCardProps> = (props) => {
  const [collapsed, setCollapsed] = createSignal(true)

  return (
    <div class="w-full rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface)] overflow-hidden">
      {/* Header */}
      <button
        type="button"
        onClick={() => setCollapsed(!collapsed())}
        class="w-full flex items-center gap-2.5 px-4 py-2 bg-[var(--surface-raised)] hover:bg-[var(--alpha-white-5)] transition-colors text-left"
      >
        <div
          class="p-1 rounded-[var(--radius-sm)] flex-shrink-0"
          style={{ background: 'rgba(139, 92, 246, 0.12)' }}
        >
          <ClipboardList class="w-3.5 h-3.5" style={{ color: '#8B5CF6' }} />
        </div>
        <span
          class="text-[10px] font-semibold tracking-widest uppercase flex-shrink-0"
          style={{ color: '#8B5CF6' }}
        >
          Plan
        </span>
        <Show when={props.plan.codename}>
          <span class="text-[12px] font-semibold flex-shrink-0" style={{ color: '#8B5CF6' }}>
            {props.plan.codename}
          </span>
          <span class="text-[10px] text-[var(--text-muted)] flex-shrink-0">&mdash;</span>
        </Show>
        <span class="text-[12px] text-[var(--text-primary)] truncate flex-1">
          {props.plan.summary}
        </span>
        <span class="text-[10px] text-[var(--text-muted)] flex-shrink-0">
          {props.plan.steps.length} step{props.plan.steps.length !== 1 ? 's' : ''}
        </span>
        <Show
          when={collapsed()}
          fallback={<ChevronUp class="w-3.5 h-3.5 text-[var(--text-muted)]" />}
        >
          <ChevronDown class="w-3.5 h-3.5 text-[var(--text-muted)]" />
        </Show>
      </button>

      <Show when={!collapsed()}>
        <div class="px-4 py-2.5 border-t border-[var(--border-subtle)]">
          <ol class="space-y-1 list-none m-0 p-0">
            <For each={props.plan.steps}>
              {(step, index) => {
                const actionConfig = () => ACTION_COLORS[step.action]
                return (
                  <li class="flex items-start gap-2">
                    <div
                      class="mt-0.5 w-4 h-4 rounded border-2 flex items-center justify-center flex-shrink-0"
                      classList={{
                        'border-[var(--accent)] bg-[var(--accent)]': step.approved,
                        'border-[var(--border-default)]': !step.approved,
                      }}
                    >
                      <Show when={step.approved}>
                        <Check class="w-2.5 h-2.5 text-white" />
                      </Show>
                    </div>
                    <span
                      class="text-[11px] font-medium tabular-nums flex-shrink-0 pt-px"
                      style={{ color: 'var(--text-muted)' }}
                    >
                      {index() + 1}.
                    </span>
                    <span class="text-[12px] text-[var(--text-primary)] flex-1">
                      {step.description}
                    </span>
                    <span
                      class="inline-flex items-center px-1.5 py-0.5 rounded text-[9px] font-medium tracking-wide uppercase flex-shrink-0"
                      style={{ background: actionConfig().bg, color: actionConfig().text }}
                    >
                      {actionConfig().label}
                    </span>
                  </li>
                )
              }}
            </For>
          </ol>
          <Show when={props.plan.estimatedTurns || props.plan.estimatedBudgetUsd}>
            <div class="mt-2 pt-2 border-t border-[var(--border-subtle)] flex items-center gap-2 text-[10px] text-[var(--text-muted)]">
              <span>~{props.plan.estimatedTurns} turns</span>
              <Show when={props.plan.estimatedBudgetUsd != null}>
                <span>&middot;</span>
                <span>~${props.plan.estimatedBudgetUsd!.toFixed(2)}</span>
              </Show>
            </div>
          </Show>
          <Show when={props.onViewFull}>
            <div class="mt-2 pt-2 border-t border-[var(--border-subtle)] flex justify-end">
              <button
                type="button"
                onClick={() => props.onViewFull?.()}
                class="text-[11px] font-medium transition-colors hover:underline"
                style={{ color: '#8B5CF6' }}
              >
                View Full Plan &rarr;
              </button>
            </div>
          </Show>
        </div>
      </Show>
    </div>
  )
}

export default InlinePlanCard
