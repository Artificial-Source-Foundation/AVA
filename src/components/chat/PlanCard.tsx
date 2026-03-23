/**
 * Plan Card Component
 *
 * Plannotator-style interactive plan viewer rendered inline in the chat.
 * Shows when the agent proposes a plan (via plan_created event).
 * Users can approve, reject with feedback, edit steps, and add per-step comments.
 *
 * Design:
 * - Header: "PLAN" label + summary
 * - Ordered steps with checkboxes, action badges, file badges, dependency indicators
 * - Per-step comment input (click step to toggle)
 * - Footer: estimated turns + budget + action buttons
 */

import {
  Check,
  ChevronDown,
  ChevronUp,
  ClipboardList,
  GripVertical,
  MessageSquare,
  Pencil,
} from 'lucide-solid'
import { type Component, createEffect, createSignal, For, on, Show } from 'solid-js'
import type { PlanData, PlanStep, PlanStepAction } from '../../types/rust-ipc'

// ============================================================================
// Types
// ============================================================================

export interface PlanCardProps {
  plan: PlanData
  onApprove: (plan: PlanData, stepComments: Record<string, string>) => void
  onReject: (feedback: string, stepComments: Record<string, string>) => void
  onEdit: (plan: PlanData, stepComments: Record<string, string>) => void
  onViewFull?: () => void
}

// ============================================================================
// Constants
// ============================================================================

const ACTION_COLORS: Record<PlanStepAction, { bg: string; text: string; label: string }> = {
  research: { bg: 'rgba(6, 182, 212, 0.12)', text: '#06B6D4', label: 'Research' },
  implement: { bg: 'rgba(59, 130, 246, 0.12)', text: '#3B82F6', label: 'Implement' },
  test: { bg: 'rgba(34, 197, 94, 0.12)', text: '#22C55E', label: 'Test' },
  review: { bg: 'rgba(245, 158, 11, 0.12)', text: '#F59E0B', label: 'Review' },
}

// ============================================================================
// Sub-components
// ============================================================================

const ActionBadge: Component<{ action: PlanStepAction }> = (props) => {
  const config = () => ACTION_COLORS[props.action]

  return (
    <span
      class="inline-flex items-center px-1.5 py-0.5 rounded text-[10px] font-medium tracking-wide uppercase flex-shrink-0"
      style={{ background: config().bg, color: config().text }}
    >
      {config().label}
    </span>
  )
}

const FilesBadge: Component<{ files: string[] }> = (props) => (
  <Show when={props.files.length > 0}>
    <span
      class="inline-flex items-center px-1.5 py-0.5 rounded text-[10px] tracking-wide flex-shrink-0"
      style={{
        background: 'var(--alpha-white-5)',
        color: 'var(--text-muted)',
        'font-family': 'var(--font-ui-mono)',
      }}
    >
      {props.files.length} file{props.files.length !== 1 ? 's' : ''}
    </span>
  </Show>
)

const DependencyIndicator: Component<{ dependsOn: string[]; allSteps: PlanStep[] }> = (props) => {
  const depLabels = () =>
    props.dependsOn
      .map((depId) => {
        const idx = props.allSteps.findIndex((s) => s.id === depId)
        return idx >= 0 ? `#${idx + 1}` : depId
      })
      .join(', ')

  return (
    <Show when={props.dependsOn.length > 0}>
      <span
        class="inline-flex items-center px-1.5 py-0.5 rounded text-[10px] flex-shrink-0"
        style={{
          background: 'var(--alpha-white-3)',
          color: 'var(--text-muted)',
        }}
        title={`Depends on step${props.dependsOn.length > 1 ? 's' : ''} ${depLabels()}`}
      >
        after {depLabels()}
      </span>
    </Show>
  )
}

const StepCommentInput: Component<{
  stepId: string
  value: string
  onInput: (stepId: string, value: string) => void
}> = (props) => {
  let inputRef: HTMLTextAreaElement | undefined

  createEffect(() => {
    if (inputRef) inputRef.focus()
  })

  return (
    <div class="mt-1.5 ml-7" style={{ animation: 'planStepExpand 120ms ease-out' }}>
      <textarea
        ref={inputRef}
        value={props.value}
        onInput={(e) => props.onInput(props.stepId, e.currentTarget.value)}
        placeholder="Add a comment on this step..."
        rows={2}
        class="
          w-full resize-none
          rounded-[var(--radius-md)]
          border border-[var(--border-subtle)]
          bg-[var(--surface-sunken)]
          px-2.5 py-1.5
          text-[12px] text-[var(--text-primary)]
          placeholder:text-[var(--text-muted)]
          focus:outline-none focus:border-[var(--accent)]
          transition-colors
        "
      />
    </div>
  )
}

// ============================================================================
// Main Component
// ============================================================================

export const PlanCard: Component<PlanCardProps> = (props) => {
  const [steps, setSteps] = createSignal<PlanStep[]>([])
  const [stepComments, setStepComments] = createSignal<Record<string, string>>({})
  const [commentingStepId, setCommentingStepId] = createSignal<string | null>(null)
  const [isEditing, setIsEditing] = createSignal(false)
  const [isRejecting, setIsRejecting] = createSignal(false)
  const [rejectFeedback, setRejectFeedback] = createSignal('')
  const [collapsed, setCollapsed] = createSignal(false)

  // Initialize steps from plan
  createEffect(
    on(
      () => props.plan,
      (plan) => {
        setSteps(plan.steps.map((s) => ({ ...s })))
        setStepComments({})
        setCommentingStepId(null)
        setIsEditing(false)
        setIsRejecting(false)
        setRejectFeedback('')
      }
    )
  )

  const toggleStepApproval = (stepId: string): void => {
    setSteps((prev) => prev.map((s) => (s.id === stepId ? { ...s, approved: !s.approved } : s)))
  }

  const updateStepDescription = (stepId: string, description: string): void => {
    setSteps((prev) => prev.map((s) => (s.id === stepId ? { ...s, description } : s)))
  }

  const handleStepComment = (stepId: string, value: string): void => {
    setStepComments((prev) => ({ ...prev, [stepId]: value }))
  }

  const toggleComment = (stepId: string): void => {
    setCommentingStepId((prev) => (prev === stepId ? null : stepId))
  }

  const buildPlanData = (): PlanData => ({
    summary: props.plan.summary,
    steps: steps(),
    estimatedTurns: props.plan.estimatedTurns,
    estimatedBudgetUsd: props.plan.estimatedBudgetUsd,
  })

  const handleApprove = (): void => {
    // Mark all steps as approved
    setSteps((prev) => prev.map((s) => ({ ...s, approved: true })))
    props.onApprove(buildPlanData(), stepComments())
  }

  const handleReject = (): void => {
    if (isRejecting()) {
      props.onReject(rejectFeedback(), stepComments())
      setIsRejecting(false)
    } else {
      setIsRejecting(true)
    }
  }

  const handleEdit = (): void => {
    if (isEditing()) {
      props.onEdit(buildPlanData(), stepComments())
      setIsEditing(false)
    } else {
      setIsEditing(true)
    }
  }

  let rejectInputRef: HTMLTextAreaElement | undefined

  createEffect(() => {
    if (isRejecting() && rejectInputRef) {
      rejectInputRef.focus()
    }
  })

  return (
    <div
      class="w-full rounded-[var(--radius-lg)] border border-[var(--border-default)] bg-[var(--surface)] shadow-[var(--shadow-sm)] overflow-hidden"
      style={{ animation: 'planCardIn 200ms ease-out' }}
    >
      {/* Header */}
      <div class="flex items-center gap-2.5 px-4 py-2.5 border-b border-[var(--border-subtle)] bg-[var(--surface-raised)]">
        <div
          class="p-1 rounded-[var(--radius-sm)] flex-shrink-0"
          style={{ background: 'rgba(139, 92, 246, 0.12)' }}
        >
          <ClipboardList class="w-4 h-4" style={{ color: '#8B5CF6' }} />
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
        <span class="text-[13px] font-medium text-[var(--text-primary)] truncate flex-1">
          {props.plan.summary}
        </span>
        <button
          type="button"
          onClick={() => setCollapsed(!collapsed())}
          class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors flex-shrink-0"
          title={collapsed() ? 'Expand plan' : 'Collapse plan'}
        >
          <Show when={collapsed()} fallback={<ChevronUp class="w-3.5 h-3.5" />}>
            <ChevronDown class="w-3.5 h-3.5" />
          </Show>
        </button>
      </div>

      <Show when={!collapsed()}>
        {/* Steps */}
        <div class="px-4 py-3">
          <ol class="space-y-1.5 list-none m-0 p-0">
            <For each={steps()}>
              {(step, index) => (
                <li class="group/step">
                  <div class="flex items-start gap-2">
                    {/* Drag handle (visual only for now) */}
                    <Show when={isEditing()}>
                      <div class="pt-0.5 cursor-grab text-[var(--text-muted)] opacity-0 group-hover/step:opacity-100 transition-opacity flex-shrink-0">
                        <GripVertical class="w-3 h-3" />
                      </div>
                    </Show>

                    {/* Checkbox */}
                    <button
                      type="button"
                      onClick={() => toggleStepApproval(step.id)}
                      class="mt-0.5 w-4 h-4 rounded border-2 flex items-center justify-center flex-shrink-0 transition-colors duration-100"
                      classList={{
                        'border-[var(--accent)] bg-[var(--accent)]': step.approved,
                        'border-[var(--border-default)] hover:border-[var(--accent)]':
                          !step.approved,
                      }}
                      title={step.approved ? 'Mark as pending' : 'Approve step'}
                    >
                      <Show when={step.approved}>
                        <Check class="w-2.5 h-2.5 text-white" />
                      </Show>
                    </button>

                    {/* Step number */}
                    <span
                      class="text-[11px] font-medium tabular-nums flex-shrink-0 pt-px"
                      style={{ color: 'var(--text-muted)' }}
                    >
                      {index() + 1}.
                    </span>

                    {/* Step content */}
                    <div class="flex-1 min-w-0">
                      <div class="flex items-center gap-1.5 flex-wrap">
                        <Show
                          when={isEditing()}
                          fallback={
                            <button
                              type="button"
                              onClick={() => toggleComment(step.id)}
                              class="text-[13px] text-[var(--text-primary)] text-left hover:text-[var(--accent)] transition-colors cursor-pointer"
                              title="Click to add comment"
                            >
                              {step.description}
                            </button>
                          }
                        >
                          <input
                            type="text"
                            value={step.description}
                            onInput={(e) => updateStepDescription(step.id, e.currentTarget.value)}
                            class="flex-1 min-w-0 text-[13px] text-[var(--text-primary)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-sm)] px-2 py-0.5 focus:outline-none focus:border-[var(--accent)] transition-colors"
                          />
                        </Show>

                        <ActionBadge action={step.action} />
                        <FilesBadge files={step.files} />
                        <DependencyIndicator dependsOn={step.dependsOn} allSteps={steps()} />
                      </div>

                      {/* Files list (expanded) */}
                      <Show when={step.files.length > 0}>
                        <div class="mt-1 flex flex-wrap gap-1">
                          <For each={step.files}>
                            {(file) => (
                              <span
                                class="text-[10px] px-1 py-0.5 rounded"
                                style={{
                                  background: 'var(--alpha-white-3)',
                                  color: 'var(--text-muted)',
                                  'font-family': 'var(--font-ui-mono)',
                                }}
                              >
                                {file}
                              </span>
                            )}
                          </For>
                        </div>
                      </Show>

                      {/* Comment indicator */}
                      <Show when={stepComments()[step.id] && commentingStepId() !== step.id}>
                        <div class="mt-1 flex items-center gap-1 text-[10px] text-[var(--text-muted)]">
                          <MessageSquare class="w-3 h-3" />
                          <span class="truncate">{stepComments()[step.id]}</span>
                        </div>
                      </Show>
                    </div>

                    {/* Comment toggle (when not editing) */}
                    <Show when={!isEditing()}>
                      <button
                        type="button"
                        onClick={() => toggleComment(step.id)}
                        class="mt-0.5 p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] opacity-0 group-hover/step:opacity-100 hover:text-[var(--accent)] hover:bg-[var(--alpha-white-5)] transition-all flex-shrink-0"
                        title="Add comment"
                      >
                        <MessageSquare class="w-3 h-3" />
                      </button>
                    </Show>
                  </div>

                  {/* Comment input */}
                  <Show when={commentingStepId() === step.id}>
                    <StepCommentInput
                      stepId={step.id}
                      value={stepComments()[step.id] ?? ''}
                      onInput={handleStepComment}
                    />
                  </Show>
                </li>
              )}
            </For>
          </ol>
        </div>

        {/* Rejection feedback area */}
        <Show when={isRejecting()}>
          <div class="px-4 pb-3" style={{ animation: 'planStepExpand 120ms ease-out' }}>
            <textarea
              ref={rejectInputRef}
              value={rejectFeedback()}
              onInput={(e) => setRejectFeedback(e.currentTarget.value)}
              placeholder="Why are you rejecting? Provide feedback..."
              rows={3}
              class="
                w-full resize-none
                rounded-[var(--radius-md)]
                border border-[var(--border-default)]
                bg-[var(--surface-sunken)]
                px-3 py-2
                text-[12px] text-[var(--text-primary)]
                placeholder:text-[var(--text-muted)]
                focus:outline-none focus:border-[var(--error)]
                transition-colors
              "
              onKeyDown={(e) => {
                if (e.key === 'Enter' && !e.shiftKey && rejectFeedback().trim()) {
                  e.preventDefault()
                  handleReject()
                }
                if (e.key === 'Escape') {
                  setIsRejecting(false)
                }
              }}
            />
          </div>
        </Show>

        {/* Footer */}
        <div class="flex items-center gap-3 px-4 py-2.5 border-t border-[var(--border-subtle)] bg-[var(--surface-raised)]">
          {/* Estimates */}
          <div class="flex items-center gap-2 text-[11px] text-[var(--text-muted)] flex-1">
            <span>
              ~{props.plan.estimatedTurns} turn{props.plan.estimatedTurns !== 1 ? 's' : ''}
            </span>
            <Show when={props.plan.estimatedBudgetUsd != null}>
              <span>&middot;</span>
              <span>~${props.plan.estimatedBudgetUsd!.toFixed(2)}</span>
            </Show>
            <Show when={steps().some((s) => s.approved)}>
              <span>&middot;</span>
              <span>
                {steps().filter((s) => s.approved).length}/{steps().length} approved
              </span>
            </Show>
          </div>

          {/* View Full Plan link */}
          <Show when={props.onViewFull}>
            <button
              type="button"
              onClick={() => props.onViewFull?.()}
              class="text-[11px] font-medium transition-colors hover:underline mr-2"
              style={{ color: '#8B5CF6' }}
            >
              View Full Plan &rarr;
            </button>
          </Show>

          {/* Action buttons */}
          <div class="flex items-center gap-1.5">
            {/* Reject */}
            <Show
              when={!isRejecting()}
              fallback={
                <button
                  type="button"
                  onClick={handleReject}
                  disabled={!rejectFeedback().trim()}
                  class="text-[11px] font-medium text-[var(--error)] hover:underline disabled:opacity-40 disabled:no-underline transition-opacity px-1"
                >
                  Send Rejection
                </button>
              }
            >
              <button
                type="button"
                onClick={handleReject}
                class="text-[11px] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors px-1"
              >
                Reject
              </button>
            </Show>

            {/* Edit */}
            <button
              type="button"
              onClick={handleEdit}
              class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] border border-[var(--border-default)] bg-[var(--surface)] px-2.5 py-1 text-[11px] font-medium text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)] transition-colors"
            >
              <Pencil class="w-3 h-3" />
              {isEditing() ? 'Save Edits' : 'Edit'}
            </button>

            {/* Approve */}
            <button
              type="button"
              onClick={handleApprove}
              class="inline-flex items-center gap-1 rounded-[var(--radius-sm)] px-2.5 py-1 text-[11px] font-medium text-white transition-colors"
              style={{ background: 'var(--accent)' }}
            >
              <Check class="w-3 h-3" />
              Approve
            </button>
          </div>
        </div>
      </Show>
    </div>
  )
}

export default PlanCard
