/**
 * Plan Card Component
 *
 * Plannotator-style interactive plan viewer rendered inline in the chat.
 * Shows when the agent proposes a plan (via plan_created event).
 * Users can approve, reject with feedback, edit steps, and add per-step comments.
 *
 * Design (Pencil KV8QP):
 * - 680px card, rounded-lg, bg surface, border border-default
 * - Header: 48px, bg background-subtle, list-checks icon (blue), "PLAN" badge, codename, estimate + chevron
 * - Summary paragraph
 * - Steps list with numbered circles (color-coded by action), action tag badges, file counts, deps
 * - Expandable step detail with description, files list, comments section
 * - Footer: step count + estimates left, Reject/Edit/Approve buttons right
 */

import {
  Check,
  ChevronDown,
  ChevronUp,
  Circle,
  CircleCheck,
  File,
  ListChecks,
  MessageCircle,
  Pencil,
  Send,
  X,
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

interface StepComment {
  id: string
  author: 'ava' | 'user'
  text: string
  time: string
}

// ============================================================================
// Constants
// ============================================================================

const ACTION_COLORS: Record<
  PlanStepAction,
  { bg: string; text: string; label: string; numBg: string }
> = {
  research: {
    bg: 'rgba(0, 188, 212, 0.125)',
    text: '#00BCD4',
    label: 'research',
    numBg: 'rgba(0, 188, 212, 0.125)',
  },
  implement: {
    bg: 'rgba(10, 132, 255, 0.125)',
    text: 'var(--accent)',
    label: 'implement',
    numBg: 'rgba(10, 132, 255, 0.125)',
  },
  test: {
    bg: 'rgba(52, 199, 89, 0.125)',
    text: 'var(--success)',
    label: 'test',
    numBg: 'rgba(52, 199, 89, 0.125)',
  },
  review: {
    bg: 'rgba(245, 166, 35, 0.125)',
    text: 'var(--warning)',
    label: 'review',
    numBg: 'rgba(245, 166, 35, 0.125)',
  },
}

// ============================================================================
// Sub-components
// ============================================================================

/** Small action tag badge (e.g. "research", "implement") */
const ActionTag: Component<{ action: PlanStepAction }> = (props) => {
  const config = () => ACTION_COLORS[props.action]
  return (
    <span
      class="inline-flex items-center rounded-[6px] text-[9px] font-medium tracking-wide flex-shrink-0"
      style={{ background: config().bg, color: config().text, padding: '2px 6px' }}
    >
      {config().label}
    </span>
  )
}

/** Numbered circle for a step, color-coded by action and status */
const StepNumber: Component<{ index: number; step: PlanStep }> = (props) => {
  const config = () => ACTION_COLORS[props.step.action]
  const numColor = () => {
    if (props.step.approved) return 'var(--success)'
    return config().text
  }
  const bgColor = () => {
    if (props.step.approved) return 'rgba(52, 199, 89, 0.125)'
    return config().numBg
  }

  return (
    <div
      class="flex items-center justify-center rounded-full flex-shrink-0"
      style={{
        width: '22px',
        height: '22px',
        background: bgColor(),
      }}
    >
      <span
        class="text-[10px] font-semibold"
        style={{
          color: numColor(),
          'font-family': 'var(--font-ui-mono)',
        }}
      >
        {props.index + 1}
      </span>
    </div>
  )
}

/** Status icon on the right side of a step row */
const StepStatusIcon: Component<{ approved: boolean }> = (props) => (
  <Show
    when={props.approved}
    fallback={<Circle class="w-4 h-4 flex-shrink-0" style={{ color: 'var(--text-muted)' }} />}
  >
    <CircleCheck class="w-4 h-4 flex-shrink-0" style={{ color: 'var(--success)' }} />
  </Show>
)

/** File row in step detail */
const FileRow: Component<{ path: string }> = (props) => (
  <div
    class="flex items-center gap-2 rounded-[var(--radius-sm)] h-7"
    style={{
      background: 'var(--surface-raised)',
      padding: '0 10px',
    }}
  >
    <File class="w-3 h-3 flex-shrink-0" style={{ color: 'var(--text-muted)' }} />
    <span
      class="text-[11px] truncate"
      style={{
        color: 'var(--text-secondary)',
        'font-family': 'var(--font-ui-mono)',
      }}
    >
      {props.path}
    </span>
  </div>
)

/** Single comment bubble in step detail */
const CommentBubble: Component<{ comment: StepComment }> = (props) => {
  const isAva = () => props.comment.author === 'ava'
  return (
    <div
      class="flex items-start gap-2.5 rounded-[var(--radius-sm)] w-full"
      style={{
        background: 'var(--surface-raised)',
        padding: '8px 10px',
      }}
    >
      {/* Avatar */}
      <div
        class="flex items-center justify-center rounded-full flex-shrink-0"
        style={{
          width: '20px',
          height: '20px',
          background: isAva()
            ? 'linear-gradient(180deg, var(--accent) 0%, var(--system-purple) 100%)'
            : 'var(--gray-6)',
        }}
      >
        <span class="text-[9px] font-bold text-white">{isAva() ? 'A' : 'X'}</span>
      </div>

      {/* Body */}
      <div class="flex flex-col gap-0.5 flex-1 min-w-0">
        <div class="flex items-center gap-1.5">
          <span class="text-[10px] font-semibold" style={{ color: 'var(--text-primary)' }}>
            {isAva() ? 'AVA' : 'You'}
          </span>
          <span
            class="text-[9px]"
            style={{
              color: 'var(--text-muted)',
              'font-family': 'var(--font-ui-mono)',
            }}
          >
            {props.comment.time}
          </span>
        </div>
        <p class="text-[11px] m-0 leading-relaxed" style={{ color: 'var(--text-secondary)' }}>
          {props.comment.text}
        </p>
      </div>
    </div>
  )
}

/** Comment input row at bottom of step detail */
const CommentInput: Component<{
  value: string
  onInput: (value: string) => void
  onSend: () => void
}> = (props) => {
  let inputRef: HTMLInputElement | undefined

  createEffect(() => {
    if (inputRef) inputRef.focus()
  })

  return (
    <div
      class="flex items-center gap-2 rounded-[var(--radius-sm)] border border-[var(--border-default)] w-full"
      style={{
        height: '36px',
        background: 'var(--surface-raised)',
        padding: '0 10px 0 12px',
      }}
    >
      <MessageCircle class="w-3.5 h-3.5 flex-shrink-0" style={{ color: 'var(--text-muted)' }} />
      <input
        ref={inputRef}
        type="text"
        value={props.value}
        onInput={(e) => props.onInput(e.currentTarget.value)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' && props.value.trim()) {
            e.preventDefault()
            props.onSend()
          }
        }}
        placeholder="Add a comment on this step..."
        class="flex-1 min-w-0 bg-transparent border-none outline-none text-[11px] text-[var(--text-primary)] placeholder:text-[var(--text-muted)] placeholder:italic"
      />
      <button
        type="button"
        onClick={() => {
          if (props.value.trim()) props.onSend()
        }}
        class="flex-shrink-0 p-0 bg-transparent border-none cursor-pointer"
        style={{ color: props.value.trim() ? 'var(--accent)' : 'var(--text-muted)' }}
      >
        <Send class="w-3.5 h-3.5" />
      </button>
    </div>
  )
}

// ============================================================================
// Main Component
// ============================================================================

export const PlanCard: Component<PlanCardProps> = (props) => {
  const [steps, setSteps] = createSignal<PlanStep[]>([])
  const [stepComments, setStepComments] = createSignal<Record<string, string>>({})
  const [commentThreads, setCommentThreads] = createSignal<Record<string, StepComment[]>>({})
  const [expandedStepId, setExpandedStepId] = createSignal<string | null>(null)
  const [commentInput, setCommentInput] = createSignal('')
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
        setCommentThreads({})
        setExpandedStepId(null)
        setCommentInput('')
        setIsEditing(false)
        setIsRejecting(false)
        setRejectFeedback('')
      }
    )
  )

  const toggleStepExpansion = (stepId: string): void => {
    setExpandedStepId((prev) => (prev === stepId ? null : stepId))
    setCommentInput('')
  }

  const updateStepDescription = (stepId: string, description: string): void => {
    setSteps((prev) => prev.map((s) => (s.id === stepId ? { ...s, description } : s)))
  }

  const handleSendComment = (stepId: string): void => {
    const text = commentInput().trim()
    if (!text) return

    const comment: StepComment = {
      id: `${stepId}-${Date.now()}`,
      author: 'user',
      text,
      time: 'just now',
    }

    setCommentThreads((prev) => ({
      ...prev,
      [stepId]: [...(prev[stepId] ?? []), comment],
    }))

    // Also store in flat stepComments for the callbacks
    setStepComments((prev) => {
      const existing = prev[stepId] ? `${prev[stepId]}\n${text}` : text
      return { ...prev, [stepId]: existing }
    })

    setCommentInput('')
  }

  const buildPlanData = (): PlanData => ({
    summary: props.plan.summary,
    steps: steps(),
    estimatedTurns: props.plan.estimatedTurns,
    estimatedBudgetUsd: props.plan.estimatedBudgetUsd,
  })

  const handleApprove = (): void => {
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

  const depLabel = (depId: string): string => {
    const idx = steps().findIndex((s) => s.id === depId)
    return idx >= 0 ? `#${idx + 1}` : depId
  }

  return (
    <div
      class="w-full overflow-hidden"
      style={{
        'max-width': '680px',
        'border-radius': 'var(--radius-lg)',
        border: '1px solid var(--border-default)',
        background: 'var(--surface)',
        animation: 'planCardIn 200ms ease-out',
      }}
    >
      {/* ── Header (48px) ────────────────────────────────────────── */}
      <div
        class="flex items-center justify-between"
        style={{
          height: '48px',
          padding: '0 16px',
          background: 'var(--surface-raised)',
          'border-bottom': '1px solid var(--border-subtle)',
        }}
      >
        {/* Left: icon + PLAN badge + codename */}
        <div class="flex items-center gap-2.5 min-w-0 h-full">
          <ListChecks class="w-4 h-4 flex-shrink-0" style={{ color: 'var(--accent)' }} />
          <span
            class="inline-flex items-center rounded-lg text-[9px] font-bold tracking-wider uppercase flex-shrink-0"
            style={{
              background: 'rgba(10, 132, 255, 0.08)',
              color: 'var(--accent)',
              padding: '2px 8px',
              'letter-spacing': '1px',
            }}
          >
            Plan
          </span>
          <Show when={props.plan.codename}>
            <span
              class="text-[13px] font-semibold truncate"
              style={{ color: 'var(--text-primary)' }}
            >
              {props.plan.codename}
            </span>
          </Show>
        </div>

        {/* Right: estimate + chevron */}
        <div class="flex items-center gap-1.5 flex-shrink-0 h-full">
          <span
            class="text-[10px]"
            style={{
              color: 'var(--text-muted)',
              'font-family': 'var(--font-ui-mono)',
            }}
          >
            ~{props.plan.estimatedTurns} turn{props.plan.estimatedTurns !== 1 ? 's' : ''}
            <Show when={props.plan.estimatedBudgetUsd != null}>
              {' '}
              &middot; ${props.plan.estimatedBudgetUsd!.toFixed(2)}
            </Show>
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
      </div>

      <Show when={!collapsed()}>
        {/* ── Summary ────────────────────────────────────────────── */}
        <div style={{ padding: '12px 16px' }}>
          <p class="m-0 text-[12px] leading-relaxed" style={{ color: 'var(--text-secondary)' }}>
            {props.plan.summary}
          </p>
        </div>

        {/* ── Divider ────────────────────────────────────────────── */}
        <div style={{ height: '1px', background: 'var(--border-subtle)' }} />

        {/* ── Steps ──────────────────────────────────────────────── */}
        <div class="flex flex-col" style={{ gap: '4px', padding: '8px' }}>
          <For each={steps()}>
            {(step, index) => {
              const isExpanded = () => expandedStepId() === step.id
              const isSelected = () => expandedStepId() === step.id

              return (
                <div
                  class="overflow-hidden transition-[border-color]"
                  style={{
                    'border-radius': 'var(--radius-sm)',
                    border: isSelected()
                      ? '1px solid var(--accent-border)'
                      : '1px solid transparent',
                    background: isExpanded() ? 'var(--surface)' : undefined,
                  }}
                >
                  {/* Step row */}
                  <button
                    type="button"
                    onClick={() => {
                      if (!isEditing()) toggleStepExpansion(step.id)
                    }}
                    class="flex items-center gap-2.5 w-full text-left transition-colors group/step"
                    style={{
                      padding: '10px 12px',
                      background: isExpanded() ? 'var(--surface-raised)' : 'var(--surface-raised)',
                      'border-radius': isExpanded()
                        ? 'var(--radius-sm) var(--radius-sm) 0 0'
                        : 'var(--radius-sm)',
                      cursor: isEditing() ? 'default' : 'pointer',
                      border: 'none',
                    }}
                  >
                    <StepNumber index={index()} step={step} />

                    {/* Step body */}
                    <div class="flex-1 min-w-0 flex flex-col" style={{ gap: '4px' }}>
                      {/* Title */}
                      <Show
                        when={isEditing()}
                        fallback={
                          <span
                            class="text-[12px] font-medium"
                            style={{ color: 'var(--text-primary)' }}
                          >
                            {step.description}
                          </span>
                        }
                      >
                        <input
                          type="text"
                          value={step.description}
                          onInput={(e) => updateStepDescription(step.id, e.currentTarget.value)}
                          onClick={(e) => e.stopPropagation()}
                          class="flex-1 min-w-0 text-[12px] text-[var(--text-primary)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-sm)] px-2 py-0.5 focus:outline-none focus:border-[var(--accent)] transition-colors"
                        />
                      </Show>

                      {/* Meta row: action tag + file count + deps */}
                      <div class="flex items-center" style={{ gap: '8px' }}>
                        <ActionTag action={step.action} />
                        <Show when={step.files.length > 0}>
                          <span
                            class="text-[9px] flex-shrink-0"
                            style={{
                              color: 'var(--text-muted)',
                              'font-family': 'var(--font-ui-mono)',
                            }}
                          >
                            {step.files.length} file{step.files.length !== 1 ? 's' : ''}
                          </span>
                        </Show>
                        <Show when={step.dependsOn.length > 0}>
                          <span
                            class="text-[9px] flex-shrink-0"
                            style={{
                              color: 'var(--text-muted)',
                              'font-family': 'var(--font-ui-mono)',
                            }}
                          >
                            after {step.dependsOn.map(depLabel).join(', ')}
                          </span>
                        </Show>
                      </div>
                    </div>

                    {/* Right: status icon or chevron */}
                    <Show
                      when={isExpanded()}
                      fallback={<StepStatusIcon approved={step.approved} />}
                    >
                      <ChevronUp
                        class="w-3.5 h-3.5 flex-shrink-0"
                        style={{ color: 'var(--text-muted)' }}
                      />
                    </Show>
                  </button>

                  {/* ── Expanded Step Detail ──────────────────────── */}
                  <Show when={isExpanded()}>
                    <div
                      class="flex flex-col"
                      style={{
                        gap: '12px',
                        padding: '16px',
                        animation: 'planStepExpand 120ms ease-out',
                      }}
                    >
                      {/* Full description (if different from title, show it; otherwise skip) */}

                      {/* FILES section */}
                      <Show when={step.files.length > 0}>
                        <div class="flex flex-col" style={{ gap: '6px' }}>
                          <span
                            class="text-[9px] font-semibold tracking-wider uppercase"
                            style={{ color: 'var(--text-muted)', 'letter-spacing': '1px' }}
                          >
                            Files
                          </span>
                          <div class="flex flex-col" style={{ gap: '4px' }}>
                            <For each={step.files}>{(file) => <FileRow path={file} />}</For>
                          </div>
                        </div>
                      </Show>

                      {/* COMMENTS divider + section */}
                      <div style={{ height: '1px', background: 'var(--border-subtle)' }} />

                      <div class="flex flex-col" style={{ gap: '8px' }}>
                        <span
                          class="text-[9px] font-semibold tracking-wider uppercase"
                          style={{ color: 'var(--text-muted)', 'letter-spacing': '1px' }}
                        >
                          Comments
                        </span>

                        {/* Existing comments */}
                        <For each={commentThreads()[step.id] ?? []}>
                          {(comment) => <CommentBubble comment={comment} />}
                        </For>

                        {/* Comment input */}
                        <CommentInput
                          value={commentInput()}
                          onInput={setCommentInput}
                          onSend={() => handleSendComment(step.id)}
                        />
                      </div>
                    </div>
                  </Show>
                </div>
              )
            }}
          </For>
        </div>

        {/* ── Rejection feedback area ────────────────────────────── */}
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

        {/* ── Footer ─────────────────────────────────────────────── */}
        <div style={{ height: '1px', background: 'var(--border-subtle)' }} />

        <div class="flex items-center justify-between" style={{ padding: '12px 16px' }}>
          {/* Left: stats */}
          <div class="flex items-center" style={{ gap: '8px' }}>
            <span
              class="text-[10px]"
              style={{ color: 'var(--text-muted)', 'font-family': 'var(--font-ui-mono)' }}
            >
              {steps().length} step{steps().length !== 1 ? 's' : ''}
            </span>
            <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
              &middot;
            </span>
            <span
              class="text-[10px]"
              style={{ color: 'var(--text-muted)', 'font-family': 'var(--font-ui-mono)' }}
            >
              ~{props.plan.estimatedTurns} turn{props.plan.estimatedTurns !== 1 ? 's' : ''}
            </span>
            <Show when={props.plan.estimatedBudgetUsd != null}>
              <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                &middot;
              </span>
              <span
                class="text-[10px]"
                style={{ color: 'var(--text-muted)', 'font-family': 'var(--font-ui-mono)' }}
              >
                ${props.plan.estimatedBudgetUsd!.toFixed(2)} est.
              </span>
            </Show>
          </div>

          {/* Right: action buttons */}
          <div class="flex items-center" style={{ gap: '8px' }}>
            {/* View Full Plan link */}
            <Show when={props.onViewFull}>
              <button
                type="button"
                onClick={() => props.onViewFull?.()}
                class="text-[11px] font-medium transition-colors hover:underline"
                style={{
                  color: 'var(--accent)',
                  background: 'transparent',
                  border: 'none',
                  cursor: 'pointer',
                  padding: 0,
                }}
              >
                View Full Plan &rarr;
              </button>
            </Show>

            {/* Reject */}
            <Show
              when={!isRejecting()}
              fallback={
                <button
                  type="button"
                  onClick={handleReject}
                  disabled={!rejectFeedback().trim()}
                  class="text-[11px] font-medium text-[var(--error)] hover:underline disabled:opacity-40 disabled:no-underline transition-opacity cursor-pointer"
                  style={{ background: 'transparent', border: 'none', padding: '0 4px' }}
                >
                  Send Rejection
                </button>
              }
            >
              <button
                type="button"
                onClick={handleReject}
                class="inline-flex items-center justify-center rounded-[var(--radius-sm)] text-[11px] font-medium transition-colors cursor-pointer"
                style={{
                  gap: '6px',
                  padding: '6px 14px',
                  background: 'rgba(255, 255, 255, 0.024)',
                  border: '1px solid var(--border-default)',
                  color: 'var(--text-secondary)',
                }}
              >
                <X class="w-3 h-3" style={{ color: 'var(--text-muted)' }} />
                Reject
              </button>
            </Show>

            {/* Edit */}
            <button
              type="button"
              onClick={handleEdit}
              class="inline-flex items-center justify-center rounded-[var(--radius-sm)] text-[11px] font-medium transition-colors cursor-pointer"
              style={{
                gap: '6px',
                padding: '6px 14px',
                background: 'rgba(255, 255, 255, 0.024)',
                border: '1px solid var(--border-default)',
                color: 'var(--text-secondary)',
              }}
            >
              <Pencil class="w-3 h-3" style={{ color: 'var(--text-muted)' }} />
              {isEditing() ? 'Save Edits' : 'Edit'}
            </button>

            {/* Approve */}
            <button
              type="button"
              onClick={handleApprove}
              class="inline-flex items-center justify-center rounded-[var(--radius-sm)] text-[11px] font-semibold transition-colors cursor-pointer"
              style={{
                gap: '6px',
                padding: '6px 18px',
                background: 'var(--accent)',
                border: 'none',
                color: '#ffffff',
              }}
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
