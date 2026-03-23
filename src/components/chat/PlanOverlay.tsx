/**
 * Full-Screen Plan Overlay
 *
 * Plannotator-inspired plan viewer. Renders the plan as a styled document card
 * centered on a dark canvas background, with a fixed toolbar header.
 *
 * Design reference: Plannotator (backnotprop/plannotator)
 * - Document card centered on dark canvas with shadow
 * - Fixed header toolbar with back, codename badge, and actions
 * - Step cards with action badges, file lists, dependency chains
 * - Progress bar and metadata strip
 * - Esc or Back button to close
 */

import {
  ArrowLeft,
  Check,
  ChevronDown,
  ChevronUp,
  ClipboardList,
  Clock,
  Copy,
  DollarSign,
  Download,
  FileCode,
  GitBranch,
  GitCompareArrows,
  Hash,
  MessageSquare,
  Pencil,
  PenLine,
  Play,
  Share2,
  Tag,
  Users,
  X,
  XCircle,
} from 'lucide-solid'
import {
  type Component,
  createEffect,
  createSignal,
  For,
  type JSX,
  onCleanup,
  Show,
} from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { usePlanOverlay } from '../../stores/planOverlayStore'
import type { PlanData, PlanStep, PlanStepAction, PlanSummary } from '../../types/rust-ipc'
import { diffPlans } from '../../utils/planDiff'

// ============================================================================
// Constants
// ============================================================================

const PLAN_ACCENT = '#8B5CF6'
const PLAN_ACCENT_SUBTLE = 'rgba(139, 92, 246, 0.12)'
const PLAN_ACCENT_GLOW = 'rgba(139, 92, 246, 0.08)'

const ACTION_CONFIG: Record<
  PlanStepAction,
  { bg: string; text: string; border: string; label: string }
> = {
  research: {
    bg: 'rgba(6, 182, 212, 0.10)',
    text: '#06B6D4',
    border: 'rgba(6, 182, 212, 0.25)',
    label: 'Research',
  },
  implement: {
    bg: 'rgba(59, 130, 246, 0.10)',
    text: '#3B82F6',
    border: 'rgba(59, 130, 246, 0.25)',
    label: 'Implement',
  },
  test: {
    bg: 'rgba(34, 197, 94, 0.10)',
    text: '#22C55E',
    border: 'rgba(34, 197, 94, 0.25)',
    label: 'Test',
  },
  review: {
    bg: 'rgba(245, 158, 11, 0.10)',
    text: '#F59E0B',
    border: 'rgba(245, 158, 11, 0.25)',
    label: 'Review',
  },
}

const QUICK_LABELS = [
  { id: 'clarify', emoji: '\u2753', text: 'Clarify this', color: '#EAB308' },
  { id: 'needs-tests', emoji: '\uD83E\uDDEA', text: 'Needs tests', color: '#3B82F6' },
  { id: 'out-of-scope', emoji: '\uD83D\uDEAB', text: 'Out of scope', color: '#EF4444' },
  { id: 'nice', emoji: '\uD83D\uDC4D', text: 'Nice approach', color: '#22C55E' },
  { id: 'alternatives', emoji: '\uD83D\uDD04', text: 'Consider alternatives', color: '#EC4899' },
  { id: 'verify', emoji: '\uD83D\uDD0D', text: 'Verify this', color: '#F97316' },
] as const

// ============================================================================
// Helpers
// ============================================================================

/** Format a plan as markdown text for copy/download */
function formatPlanMarkdown(plan: PlanData): string {
  return [
    `# ${plan.codename ? `${plan.codename} — ` : ''}${plan.summary}`,
    '',
    ...plan.steps.map(
      (s, i) =>
        `${i + 1}. [${s.action.toUpperCase()}] ${s.description}${s.files.length ? `\n   Files: ${s.files.join(', ')}` : ''}`
    ),
  ].join('\n')
}

/** Parse a plan markdown file back into PlanData */
function parsePlanMarkdown(content: string): PlanData | null {
  const lines = content.split('\n')
  let codename: string | undefined
  let summary = ''
  const steps: PlanStep[] = []

  // Parse frontmatter
  let inFrontmatter = false
  let frontmatterDone = false
  for (const line of lines) {
    if (line.trim() === '---') {
      if (!inFrontmatter) {
        inFrontmatter = true
        continue
      } else {
        inFrontmatter = false
        frontmatterDone = true
        continue
      }
    }
    if (inFrontmatter) {
      const codenameMatch = line.match(/^codename:\s*(.+)/)
      if (codenameMatch) codename = codenameMatch[1].trim()
      const summaryMatch = line.match(/^summary:\s*"?(.+?)"?\s*$/)
      if (summaryMatch) summary = summaryMatch[1]
      continue
    }
    if (!frontmatterDone && !inFrontmatter) continue

    // Parse steps: lines starting with ### or numbered lists with [ACTION]
    const stepMatch = line.match(/^(?:###\s+|(\d+)\.\s+)\[(\w+)]\s+(.+)/)
    if (stepMatch) {
      const action = (stepMatch[2].toLowerCase() as PlanStepAction) || 'implement'
      steps.push({
        id: `step-${steps.length + 1}`,
        description: stepMatch[3],
        files: [],
        action: ['research', 'implement', 'test', 'review'].includes(action) ? action : 'implement',
        dependsOn: [],
        approved: false,
      })
    }
    // Parse file references after a step
    const filesMatch = line.match(/^\s+Files:\s*(.+)/)
    if (filesMatch && steps.length > 0) {
      steps[steps.length - 1].files = filesMatch[1].split(',').map((f) => f.trim())
    }
  }

  if (!summary && steps.length === 0) return null
  return { summary, steps, estimatedTurns: steps.length, codename }
}

// ============================================================================
// Sub-components
// ============================================================================

/** Individual step rendered as a card with left accent border */
const StepCard: Component<{
  step: PlanStep
  index: number
  allSteps: PlanStep[]
  comment?: string
  isCommenting: boolean
  isEditing: boolean
  labels: string[]
  onToggleApproval: () => void
  onToggleComment: () => void
  onAddComment: (comment: string) => void
  onToggleEdit: () => void
  onUpdateDescription: (description: string) => void
  onMoveUp: () => void
  onMoveDown: () => void
  onAddLabel: (labelId: string) => void
  onRemoveLabel: (labelId: string) => void
  isFirst: boolean
  isLast: boolean
  diffType?: 'added' | 'removed' | 'modified' | 'unchanged'
  oldDescription?: string
}> = (props) => {
  const action = () => ACTION_CONFIG[props.step.action]
  const depLabels = () =>
    props.step.dependsOn.map((depId) => {
      const idx = props.allSteps.findIndex((s) => s.id === depId)
      return idx >= 0 ? `Step ${idx + 1}` : depId
    })

  const [editText, setEditText] = createSignal('')
  const [commentText, setCommentText] = createSignal('')
  const [labelPickerOpen, setLabelPickerOpen] = createSignal(false)

  // Initialize edit text when entering edit mode
  createEffect(() => {
    if (props.isEditing) {
      setEditText(props.step.description)
    }
  })

  // Initialize comment text from existing comment
  createEffect(() => {
    if (props.isCommenting) {
      setCommentText(props.comment ?? '')
    }
  })

  const saveEdit = (): void => {
    const text = editText().trim()
    if (text && text !== props.step.description) {
      props.onUpdateDescription(text)
    }
    props.onToggleEdit()
  }

  const cancelEdit = (): void => {
    props.onToggleEdit()
  }

  const saveComment = (): void => {
    props.onAddComment(commentText())
    props.onToggleComment()
  }

  return (
    <section
      id={`plan-step-${props.step.id}`}
      class="group/step rounded-lg border overflow-hidden transition-all duration-150"
      style={{
        background:
          props.diffType === 'added'
            ? 'rgba(34, 197, 94, 0.04)'
            : props.diffType === 'removed'
              ? 'rgba(239, 68, 68, 0.04)'
              : props.diffType === 'modified'
                ? 'rgba(245, 158, 11, 0.04)'
                : 'var(--surface)',
        'border-color':
          props.diffType === 'added'
            ? 'rgba(34, 197, 94, 0.3)'
            : props.diffType === 'removed'
              ? 'rgba(239, 68, 68, 0.3)'
              : props.diffType === 'modified'
                ? 'rgba(245, 158, 11, 0.3)'
                : props.step.approved
                  ? 'rgba(34, 197, 94, 0.3)'
                  : 'var(--border-subtle)',
        'border-left': `3px solid $
          props.diffType === 'added'
            ? '#22C55E'
            : props.diffType === 'removed'
              ? '#EF4444'
              : props.diffType === 'modified'
                ? '#F59E0B'
                : props.step.approved
                  ? '#22C55E'
                  : action().text`,
        opacity:
          props.diffType === 'unchanged' ? '0.6' : props.diffType === 'removed' ? '0.7' : undefined,
        'text-decoration': props.diffType === 'removed' ? 'line-through' : undefined,
      }}
      data-step-card
    >
      {/* Step header */}
      <div class="flex items-center gap-3 px-4 py-3">
        {/* Reorder buttons — visible on hover */}
        <div class="flex flex-col gap-0.5 flex-shrink-0 transition-opacity duration-100 opacity-0 group-hover/step:opacity-100">
          <button
            type="button"
            onClick={() => props.onMoveUp()}
            disabled={props.isFirst}
            class="p-0 border-0 rounded transition-colors"
            style={{
              color: props.isFirst ? 'var(--border-subtle)' : 'var(--text-muted)',
              cursor: props.isFirst ? 'default' : 'pointer',
              background: 'transparent',
              width: '16px',
              height: '16px',
              display: 'flex',
              'align-items': 'center',
              'justify-content': 'center',
            }}
            title="Move step up"
          >
            <ChevronUp class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={() => props.onMoveDown()}
            disabled={props.isLast}
            class="p-0 border-0 rounded transition-colors"
            style={{
              color: props.isLast ? 'var(--border-subtle)' : 'var(--text-muted)',
              cursor: props.isLast ? 'default' : 'pointer',
              background: 'transparent',
              width: '16px',
              height: '16px',
              display: 'flex',
              'align-items': 'center',
              'justify-content': 'center',
            }}
            title="Move step down"
          >
            <ChevronDown class="w-3.5 h-3.5" />
          </button>
        </div>
        {/* Step number / check — clickable to toggle approval */}
        <button
          type="button"
          onClick={() => props.onToggleApproval()}
          class="w-7 h-7 rounded-full flex items-center justify-center text-[12px] font-bold flex-shrink-0 transition-all cursor-pointer border-0 p-0"
          style={{
            background: props.step.approved ? 'rgba(34, 197, 94, 0.15)' : action().bg,
            color: props.step.approved ? '#22C55E' : action().text,
            transition: 'transform 150ms',
          }}
          title={props.step.approved ? 'Mark as unapproved' : 'Mark as approved'}
        >
          <Show when={props.step.approved} fallback={props.index + 1}>
            <Check class="w-4 h-4" />
          </Show>
        </button>

        {/* Description or edit input */}
        <Show
          when={!props.isEditing}
          fallback={
            <input
              type="text"
              value={editText()}
              onInput={(e) => setEditText(e.currentTarget.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  e.preventDefault()
                  saveEdit()
                } else if (e.key === 'Escape') {
                  e.preventDefault()
                  cancelEdit()
                }
              }}
              onBlur={() => saveEdit()}
              ref={(el) => setTimeout(() => el.focus(), 0)}
              class="text-[14px] font-medium flex-1 leading-snug rounded px-2 py-1 border outline-none"
              style={{
                color: 'var(--text-primary)',
                background: 'var(--alpha-white-5)',
                'border-color': PLAN_ACCENT,
              }}
            />
          }
        >
          <div class="flex-1">
            <Show when={props.diffType === 'modified' && props.oldDescription}>
              <span class="text-[13px] text-[var(--text-muted)] line-through block mb-0.5">
                {props.oldDescription}
              </span>
            </Show>
            <span class="text-[14px] text-[var(--text-primary)] font-medium leading-snug">
              {props.step.description}
            </span>
          </div>
        </Show>

        {/* Hover actions: edit + comment */}
        <div
          class="flex items-center gap-0.5 transition-opacity duration-100 group-hover/step:opacity-100"
          classList={{
            'opacity-100': props.isEditing || props.isCommenting,
            'opacity-0': !props.isEditing && !props.isCommenting,
          }}
        >
          <button
            type="button"
            onClick={() => props.onToggleEdit()}
            class="p-1.5 rounded-md transition-colors"
            style={{ color: props.isEditing ? PLAN_ACCENT : 'var(--text-muted)' }}
            title="Edit step description"
          >
            <Pencil class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={() => props.onToggleComment()}
            class="p-1.5 rounded-md transition-colors"
            style={{
              color: props.comment
                ? PLAN_ACCENT
                : props.isCommenting
                  ? PLAN_ACCENT
                  : 'var(--text-muted)',
            }}
            title="Add comment"
          >
            <MessageSquare class="w-3.5 h-3.5" />
          </button>
          <div class="relative">
            <button
              type="button"
              onClick={() => setLabelPickerOpen(!labelPickerOpen())}
              class="p-1.5 rounded-md transition-colors"
              style={{
                color:
                  props.labels.length > 0 || labelPickerOpen() ? PLAN_ACCENT : 'var(--text-muted)',
              }}
              title="Add label"
            >
              <Tag class="w-3.5 h-3.5" />
            </button>
            <Show when={labelPickerOpen()}>
              <div
                class="absolute right-0 top-full mt-1 z-50 rounded-lg border p-2 min-w-[180px]"
                style={{
                  background: 'var(--surface-raised)',
                  'border-color': 'var(--border-subtle)',
                  'box-shadow': '0 4px 12px rgba(0,0,0,0.2)',
                }}
              >
                <For each={QUICK_LABELS}>
                  {(label) => {
                    const isApplied = () => props.labels.includes(label.id)
                    return (
                      <button
                        type="button"
                        onClick={() => {
                          if (isApplied()) {
                            props.onRemoveLabel(label.id)
                          } else {
                            props.onAddLabel(label.id)
                          }
                        }}
                        class="flex items-center gap-2 w-full text-left px-2 py-1.5 rounded-md text-[12px] transition-colors"
                        style={{
                          color: isApplied() ? label.color : 'var(--text-secondary)',
                          background: isApplied() ? `$label.color15` : 'transparent',
                        }}
                      >
                        <span>{label.emoji}</span>
                        <span>{label.text}</span>
                        <Show when={isApplied()}>
                          <Check class="w-3 h-3 ml-auto" style={{ color: label.color }} />
                        </Show>
                      </button>
                    )
                  }}
                </For>
              </div>
            </Show>
          </div>
        </div>

        {/* Action badge */}
        <span
          class="inline-flex items-center px-2.5 py-1 rounded-full text-[10px] font-semibold tracking-wider uppercase flex-shrink-0 border"
          style={{ background: action().bg, color: action().text, 'border-color': action().border }}
        >
          {action().label}
        </span>
      </div>

      {/* Existing comment indicator */}
      <Show when={props.comment && !props.isCommenting}>
        <div class="px-4 pb-2 -mt-1 flex items-start gap-2" style={{ 'padding-left': '3.25rem' }}>
          <MessageSquare
            class="w-3 h-3 mt-0.5 flex-shrink-0"
            style={{ color: PLAN_ACCENT, opacity: '0.6' }}
          />
          <span class="text-[12px] leading-snug italic" style={{ color: 'var(--text-muted)' }}>
            {props.comment}
          </span>
        </div>
      </Show>

      {/* Applied labels */}
      <Show when={props.labels.length > 0}>
        <div
          class="px-4 pb-2 -mt-1 flex items-center gap-1.5 flex-wrap"
          style={{ 'padding-left': '3.25rem' }}
        >
          <For each={props.labels}>
            {(labelId) => {
              const label = () => QUICK_LABELS.find((l) => l.id === labelId)
              return (
                <Show when={label()}>
                  {(l) => (
                    <button
                      type="button"
                      class="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[10px] font-medium border cursor-pointer transition-opacity hover:opacity-80"
                      style={{
                        color: l().color,
                        background: `$l().color12`,
                        'border-color': `$l().color30`,
                      }}
                      onClick={() => props.onRemoveLabel(l().id)}
                      title={`Remove "${l().text}"`}
                    >
                      <span>{l().emoji}</span>
                      {l().text}
                      <X class="w-2.5 h-2.5" />
                    </button>
                  )}
                </Show>
              )
            }}
          </For>
        </div>
      </Show>

      {/* Comment textarea */}
      <Show when={props.isCommenting}>
        <div class="px-4 pb-3 -mt-1" style={{ 'padding-left': '3.25rem' }}>
          <textarea
            value={commentText()}
            onInput={(e) => setCommentText(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
                e.preventDefault()
                saveComment()
              } else if (e.key === 'Escape') {
                e.preventDefault()
                props.onToggleComment()
              }
            }}
            ref={(el) => setTimeout(() => el.focus(), 0)}
            placeholder="Add a comment for this step..."
            rows={2}
            class="w-full text-[12px] rounded-md border px-3 py-2 outline-none resize-none"
            style={{
              color: 'var(--text-primary)',
              background: 'var(--alpha-white-3)',
              'border-color': PLAN_ACCENT,
            }}
          />
          <div class="flex items-center justify-end gap-2 mt-1.5">
            <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
              Ctrl+Enter to save
            </span>
            <button
              type="button"
              onClick={() => props.onToggleComment()}
              class="text-[11px] px-2 py-0.5 rounded transition-colors"
              style={{ color: 'var(--text-muted)' }}
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={saveComment}
              class="text-[11px] px-2 py-0.5 rounded transition-colors"
              style={{ color: PLAN_ACCENT }}
            >
              Save
            </button>
          </div>
        </div>
      </Show>

      {/* Details: files + dependencies */}
      <Show when={props.step.files.length > 0 || props.step.dependsOn.length > 0}>
        <div
          class="px-4 py-2.5 space-y-2 border-t"
          style={{ 'border-color': 'var(--border-subtle)' }}
        >
          <Show when={props.step.files.length > 0}>
            <div class="flex items-start gap-2">
              <FileCode
                class="w-3.5 h-3.5 mt-0.5 flex-shrink-0"
                style={{ color: 'var(--text-muted)' }}
              />
              <div class="flex flex-wrap gap-1.5">
                <For each={props.step.files}>
                  {(file) => (
                    <span
                      class="text-[11px] px-1.5 py-0.5 rounded border"
                      style={{
                        background: 'var(--alpha-white-3)',
                        'border-color': 'var(--border-subtle)',
                        color: 'var(--text-secondary)',
                        'font-family': 'var(--font-ui-mono)',
                      }}
                    >
                      {file}
                    </span>
                  )}
                </For>
              </div>
            </div>
          </Show>
          <Show when={props.step.dependsOn.length > 0}>
            <div class="flex items-center gap-2">
              <GitBranch class="w-3.5 h-3.5 flex-shrink-0" style={{ color: 'var(--text-muted)' }} />
              <span class="text-[11px] text-[var(--text-muted)]">
                Depends on: {depLabels().join(', ')}
              </span>
            </div>
          </Show>
        </div>
      </Show>
    </section>
  )
}

/** Metadata pill in the info strip */
const MetaPill: Component<{ icon: Component<{ class?: string }>; children: JSX.Element }> = (
  props
) => (
  <div
    class="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-[11px] border"
    style={{
      background: 'var(--alpha-white-3)',
      'border-color': 'var(--border-subtle)',
      color: 'var(--text-muted)',
    }}
  >
    <props.icon class="w-3.5 h-3.5" />
    {props.children}
  </div>
)

// ============================================================================
// Main Overlay
// ============================================================================

export const PlanOverlay: Component = () => {
  const {
    activePlan,
    isOpen,
    closePlan,
    executePlan,
    refinePlan,
    stepComments,
    commentingStepId,
    stepLabels,
    addStepComment,
    toggleStepComment,
    updateStep,
    toggleStepApproval,
    moveStep,
    addStepLabel,
    removeStepLabel,
    previousPlan,
    showDiff,
    toggleDiff,
    hasDiff,
  } = usePlanOverlay()
  const agent = useAgent()
  const [copied, setCopied] = createSignal(false)
  const [shareCopied, setShareCopied] = createSignal(false)
  const [editingStepId, setEditingStepId] = createSignal<string | null>(null)
  const [showRejectInput, setShowRejectInput] = createSignal(false)
  const [rejectFeedback, setRejectFeedback] = createSignal('')
  const [planHistory, setPlanHistory] = createSignal<PlanSummary[]>([])

  // Fetch plan history when overlay opens
  createEffect(() => {
    if (isOpen()) {
      fetch('/api/plans')
        .then((r) => r.json())
        .then((plans: PlanSummary[]) => setPlanHistory(plans))
        .catch(() => {}) // silently ignore if API not available
    }
  })

  /** Load a saved plan from history by filename. */
  const loadPlanFromHistory = (filename: string): void => {
    fetch(`/api/plans/${encodeURIComponent(filename)}`)
      .then((r) => {
        if (!r.ok) throw new Error('Not found')
        return r.text()
      })
      .then((content) => {
        // Parse the markdown back into a PlanData structure
        const plan = parsePlanMarkdown(content)
        if (plan) {
          const { openPlan: openPlanFn } = usePlanOverlay()
          openPlanFn(plan)
        }
      })
      .catch(() => {}) // silently ignore errors
  }

  /** Generate a shareable URL with the plan encoded in the hash. */
  const handleShare = (): void => {
    const plan = activePlan()
    if (!plan) return
    const json = JSON.stringify(plan)
    const encoded = btoa(unescape(encodeURIComponent(json)))
    const url = `${window.location.origin}${window.location.pathname}#plan=${encoded}`
    navigator.clipboard.writeText(url).then(() => {
      setShareCopied(true)
      setTimeout(() => setShareCopied(false), 2000)
    })
  }

  const commentCount = (): number =>
    Object.keys(stepComments()).filter((k) => stepComments()[k]).length

  /** Collect step comments into a flat record for the backend. */
  const collectStepComments = (): Record<string, string> => {
    const comments = stepComments()
    const result: Record<string, string> = {}
    for (const [k, v] of Object.entries(comments)) {
      if (v) result[k] = v
    }
    return result
  }

  /** Approve and execute the plan via the agent backend. */
  const handleApprove = (mode: 'code' | 'praxis'): void => {
    const plan = activePlan()
    if (!plan) return
    agent.resolvePlan('approved', plan, undefined, collectStepComments())
    executePlan(mode)
  }

  /** Send rejection with feedback to the agent backend. */
  const handleReject = (): void => {
    const feedback = rejectFeedback().trim()
    if (!feedback) return
    // Collect step comments and include them in the rejection feedback
    const comments = collectStepComments()
    const commentEntries = Object.entries(comments)
    const commentSuffix =
      commentEntries.length > 0
        ? `\n\nStep comments:\n${commentEntries.map(([id, c]) => `- Step ${id}: ${c}`).join('\n')}`
        : ''
    agent.resolvePlan('rejected', undefined, feedback + commentSuffix, comments)
    setShowRejectInput(false)
    setRejectFeedback('')
    closePlan()
  }

  /** Refine: send as modified with step comments to the agent backend. */
  const handleRefine = (): void => {
    const plan = activePlan()
    if (!plan) return
    agent.resolvePlan('modified', plan, undefined, collectStepComments())
    refinePlan()
  }

  // Keyboard shortcuts
  createEffect(() => {
    if (!isOpen()) return
    const handleKeyDown = (e: KeyboardEvent): void => {
      if (e.key === 'Escape') {
        e.preventDefault()
        e.stopPropagation()
        if (showRejectInput()) {
          setShowRejectInput(false)
          setRejectFeedback('')
        } else {
          closePlan()
        }
        return
      }
      // Cmd/Ctrl+Shift+Enter = Execute with Praxis (check before Ctrl+Enter)
      if (e.key === 'Enter' && (e.metaKey || e.ctrlKey) && e.shiftKey) {
        e.preventDefault()
        handleApprove('praxis')
      }
      // Cmd/Ctrl+Enter = Execute plan
      if (e.key === 'Enter' && (e.metaKey || e.ctrlKey) && !e.shiftKey) {
        e.preventDefault()
        handleApprove('code')
      }
      // Cmd/Ctrl+C when nothing selected = Copy plan
      if (e.key === 'c' && (e.metaKey || e.ctrlKey) && !window.getSelection()?.toString()) {
        e.preventDefault()
        handleCopy()
      }
      // Cmd/Ctrl+S = Download plan
      if (e.key === 's' && (e.metaKey || e.ctrlKey)) {
        e.preventDefault()
        handleDownload()
      }
    }
    window.addEventListener('keydown', handleKeyDown, { capture: true })
    onCleanup(() => window.removeEventListener('keydown', handleKeyDown, { capture: true }))
  })

  const approvedCount = () => activePlan()?.steps.filter((s) => s.approved).length ?? 0
  const totalSteps = () => activePlan()?.steps.length ?? 0
  const progressPct = () => (totalSteps() > 0 ? (approvedCount() / totalSteps()) * 100 : 0)

  const handleCopy = (): void => {
    const plan = activePlan()
    if (!plan) return
    const text = formatPlanMarkdown(plan)
    navigator.clipboard.writeText(text).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 2000)
    })
  }

  const handleDownload = (): void => {
    const plan = activePlan()
    if (!plan) return
    const md = formatPlanMarkdown(plan)
    const blob = new Blob([md], { type: 'text/markdown' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `$plan.codename || 'plan'.md`
    a.click()
    URL.revokeObjectURL(url)
  }

  return (
    <Show when={isOpen() && activePlan()}>
      {(plan) => (
        <div
          class="flex flex-col overflow-hidden h-full w-full"
          style={{
            background: 'var(--bg)',
            animation: 'planOverlayIn 200ms ease-out',
          }}
        >
          {/* ── Header toolbar (Plannotator-style fixed bar) ── */}
          <div
            class="flex items-center gap-3 px-5 flex-shrink-0"
            style={{
              height: '48px',
              background: 'var(--surface-raised)',
              'border-bottom': '1px solid var(--border-subtle)',
            }}
          >
            {/* Left: Back button */}
            <button
              type="button"
              onClick={() => closePlan()}
              class="flex items-center gap-1.5 text-[13px] hover:opacity-100 transition-opacity"
              style={{ color: 'var(--text-secondary)', opacity: '0.8' }}
            >
              <ArrowLeft class="w-4 h-4" />
              <span>Back to Chat</span>
            </button>

            {/* Center: Codename badge */}
            <div class="flex-1 flex justify-center">
              <div class="flex items-center gap-2">
                <div class="p-1 rounded" style={{ background: PLAN_ACCENT_SUBTLE }}>
                  <ClipboardList class="w-4 h-4" style={{ color: PLAN_ACCENT }} />
                </div>
                <Show when={plan().codename}>
                  <span class="text-[13px] font-bold tracking-wide" style={{ color: PLAN_ACCENT }}>
                    {plan().codename}
                  </span>
                </Show>
                {/* Diff badge */}
                <Show when={hasDiff()}>
                  {(() => {
                    const prev = previousPlan()
                    const curr = plan()
                    if (!prev || !curr) return null
                    const result = diffPlans(prev, curr)
                    return (
                      <button
                        type="button"
                        onClick={() => toggleDiff()}
                        class="inline-flex items-center gap-1 px-2 py-0.5 rounded-full text-[10px] font-medium border transition-colors"
                        classList={{
                          'border-[#22C55E] text-[#22C55E] bg-[rgba(34,197,94,0.1)]': showDiff(),
                          'border-[var(--border-subtle)] text-[var(--text-muted)] hover:text-[var(--text-primary)]':
                            !showDiff(),
                        }}
                      >
                        <GitCompareArrows class="w-3 h-3" />+{result.stats.added} -
                        {result.stats.removed} ~{result.stats.modified}
                      </button>
                    )
                  })()}
                </Show>
              </div>
            </div>

            {/* Right: Actions */}
            <div class="flex items-center gap-1">
              <button
                type="button"
                onClick={handleCopy}
                class="p-2 rounded-md transition-colors"
                style={{ color: 'var(--text-muted)' }}
                title="Copy as Markdown (Ctrl+C)"
              >
                <Show when={copied()} fallback={<Copy class="w-4 h-4" />}>
                  <Check class="w-4 h-4" style={{ color: '#22C55E' }} />
                </Show>
              </button>
              <button
                type="button"
                onClick={handleDownload}
                class="p-2 rounded-md transition-colors"
                style={{ color: 'var(--text-muted)' }}
                title="Download as .md (Ctrl+S)"
              >
                <Download class="w-4 h-4" />
              </button>
              <button
                type="button"
                onClick={handleShare}
                class="p-2 rounded-md transition-colors"
                style={{ color: shareCopied() ? '#22C55E' : 'var(--text-muted)' }}
                title={shareCopied() ? 'Link copied!' : 'Copy share link'}
              >
                <Show when={shareCopied()} fallback={<Share2 class="w-4 h-4" />}>
                  <Check class="w-4 h-4" style={{ color: '#22C55E' }} />
                </Show>
              </button>
              <button
                type="button"
                onClick={() => closePlan()}
                class="p-2 rounded-md transition-colors"
                style={{ color: 'var(--text-muted)' }}
                title="Close (Esc)"
              >
                <X class="w-4 h-4" />
              </button>
            </div>
          </div>

          {/* ── Canvas area with TOC + centered document card ── */}
          <div
            class="flex-1 overflow-y-auto"
            style={{
              background: `
                radial-gradient(circle at 50% 0%, ${PLAN_ACCENT_GLOW} 0%, transparent 50%),
                var(--bg)
              `,
              'background-size': '100% 100%, 24px 24px',
            }}
          >
            <div class="flex">
              {/* TOC sidebar */}
              <div
                class="w-[200px] flex-shrink-0 sticky top-0 self-start hidden lg:block border-r"
                style={{
                  'border-color': 'var(--border-subtle)',
                  background: 'color-mix(in srgb, var(--surface) 50%, transparent)',
                }}
              >
                <div class="py-8 pl-6 pr-2">
                  <span
                    class="text-[10px] font-semibold tracking-[0.1em] uppercase mb-3 block"
                    style={{ color: 'var(--text-muted)' }}
                  >
                    Contents
                  </span>
                  <div class="space-y-1">
                    <For each={plan().steps}>
                      {(step, index) => (
                        <button
                          type="button"
                          onClick={() =>
                            document
                              .getElementById(`plan-step-${step.id}`)
                              ?.scrollIntoView({ behavior: 'smooth', block: 'center' })
                          }
                          class="w-full text-left text-[11px] py-1 px-2 rounded truncate transition-colors"
                          classList={{
                            'hover:bg-[rgba(255,255,255,0.05)]': true,
                          }}
                          style={{
                            color: step.approved ? '#22C55E' : 'var(--text-muted)',
                          }}
                        >
                          {index() + 1}. {step.description}
                        </button>
                      )}
                    </For>
                  </div>

                  {/* Plan history */}
                  <div class="mt-4">
                    <span
                      class="text-[10px] font-semibold tracking-[0.1em] uppercase mb-2 block"
                      style={{ color: 'var(--text-muted)' }}
                    >
                      History
                    </span>
                    <Show
                      when={planHistory().length > 0}
                      fallback={
                        <span class="text-[10px] text-[var(--text-muted)]">No saved plans</span>
                      }
                    >
                      <div class="space-y-1">
                        <For each={planHistory()}>
                          {(item) => (
                            <button
                              type="button"
                              onClick={() => loadPlanFromHistory(item.filename)}
                              class="w-full text-left text-[10px] py-1 px-2 rounded truncate transition-colors text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)]"
                              title={item.summary}
                            >
                              {item.codename || item.summary.slice(0, 20)}
                            </button>
                          )}
                        </For>
                      </div>
                    </Show>
                  </div>
                </div>
              </div>

              {/* Document card — fills remaining width like Plannotator */}
              <div
                class="flex-1 min-w-0 overflow-hidden"
                style={{
                  background: 'var(--surface)',
                }}
              >
                {/* ── Document header (inside card) ── */}
                <div
                  class="px-10 pt-8 pb-6"
                  style={{
                    'border-bottom': '1px solid var(--border-subtle)',
                    background: `linear-gradient(180deg, ${PLAN_ACCENT_GLOW} 0%, transparent 100%)`,
                  }}
                >
                  {/* Codename label */}
                  <Show when={plan().codename}>
                    <span
                      class="text-[11px] font-bold tracking-[0.15em] uppercase block mb-3"
                      style={{ color: PLAN_ACCENT }}
                    >
                      {plan().codename}
                    </span>
                  </Show>

                  {/* Title */}
                  <h1
                    class="text-[20px] font-semibold leading-tight mb-4"
                    style={{ color: 'var(--text-primary)' }}
                  >
                    {plan().summary}
                  </h1>

                  {/* Meta pills strip */}
                  <div class="flex items-center gap-2 flex-wrap">
                    <MetaPill icon={Hash}>
                      {totalSteps()} step{totalSteps() !== 1 ? 's' : ''}
                    </MetaPill>
                    <Show when={plan().estimatedTurns}>
                      <MetaPill icon={Clock}>~{plan().estimatedTurns} turns</MetaPill>
                    </Show>
                    <Show when={plan().estimatedBudgetUsd != null}>
                      <MetaPill icon={DollarSign}>
                        ~${plan().estimatedBudgetUsd!.toFixed(2)}
                      </MetaPill>
                    </Show>
                    <Show when={approvedCount() > 0}>
                      <div
                        class="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-[11px] border"
                        style={{
                          background: 'rgba(34, 197, 94, 0.08)',
                          'border-color': 'rgba(34, 197, 94, 0.25)',
                          color: '#22C55E',
                        }}
                      >
                        <Check class="w-3.5 h-3.5" />
                        {approvedCount()}/{totalSteps()} approved
                      </div>
                    </Show>
                  </div>

                  {/* Progress bar */}
                  <Show when={approvedCount() > 0}>
                    <div class="mt-4">
                      <div
                        class="h-1 rounded-full overflow-hidden"
                        style={{ background: 'var(--alpha-white-5)' }}
                      >
                        <div
                          class="h-full rounded-full transition-all duration-500 ease-out"
                          style={{
                            width: `${progressPct()}%`,
                            background: 'linear-gradient(90deg, #22C55E, #4ADE80)',
                          }}
                        />
                      </div>
                    </div>
                  </Show>
                </div>

                {/* ── Steps section (inside card) ── */}
                <div class="px-10 py-6">
                  <div class="flex items-center gap-2 mb-4">
                    <span
                      class="text-[11px] font-semibold tracking-[0.1em] uppercase"
                      style={{ color: 'var(--text-muted)' }}
                    >
                      Implementation Steps
                    </span>
                    <div class="flex-1 h-px" style={{ background: 'var(--border-subtle)' }} />
                  </div>

                  <div class="space-y-3">
                    {(() => {
                      const prev = previousPlan()
                      const curr = plan()
                      const diff = showDiff() && prev ? diffPlans(prev, curr) : null
                      const diffMap = diff ? new Map(diff.steps.map((d) => [d.step.id, d])) : null

                      // When in diff mode, show removed steps too
                      const stepsToRender = diff ? diff.steps.map((d) => d.step) : curr.steps

                      return (
                        <For each={stepsToRender}>
                          {(step, index) => {
                            const stepDiff = () => diffMap?.get(step.id)
                            return (
                              <StepCard
                                step={step}
                                index={index()}
                                allSteps={curr.steps}
                                comment={stepComments()[step.id]}
                                isCommenting={commentingStepId() === step.id}
                                isEditing={editingStepId() === step.id}
                                labels={stepLabels()[step.id] || []}
                                onToggleApproval={() => toggleStepApproval(step.id)}
                                onToggleComment={() => toggleStepComment(step.id)}
                                onAddComment={(comment) => addStepComment(step.id, comment)}
                                onToggleEdit={() =>
                                  setEditingStepId((prev) => (prev === step.id ? null : step.id))
                                }
                                onUpdateDescription={(desc) =>
                                  updateStep(step.id, { description: desc })
                                }
                                onMoveUp={() => moveStep(step.id, 'up')}
                                onMoveDown={() => moveStep(step.id, 'down')}
                                onAddLabel={(labelId) => addStepLabel(step.id, labelId)}
                                onRemoveLabel={(labelId) => removeStepLabel(step.id, labelId)}
                                isFirst={index() === 0}
                                isLast={index() === stepsToRender.length - 1}
                                diffType={stepDiff()?.diffType}
                                oldDescription={stepDiff()?.oldStep?.description}
                              />
                            )
                          }}
                        </For>
                      )
                    })()}
                  </div>
                </div>

                {/* ── Footer with action buttons (inside card) ── */}
                <div
                  class="px-10 py-5"
                  style={{
                    'border-top': '1px solid var(--border-subtle)',
                    background: 'var(--surface-raised)',
                  }}
                >
                  {/* Rejection feedback textarea (shown when Reject clicked) */}
                  <Show when={showRejectInput()}>
                    <div class="mb-4">
                      <textarea
                        value={rejectFeedback()}
                        onInput={(e) => setRejectFeedback(e.currentTarget.value)}
                        onKeyDown={(e) => {
                          if (e.key === 'Enter' && !e.shiftKey) {
                            e.preventDefault()
                            handleReject()
                          } else if (e.key === 'Escape') {
                            e.preventDefault()
                            e.stopPropagation()
                            setShowRejectInput(false)
                            setRejectFeedback('')
                          }
                        }}
                        ref={(el) => setTimeout(() => el.focus(), 0)}
                        placeholder="Why should this plan be rejected? What should change?"
                        rows={3}
                        class="w-full text-[13px] rounded-md border px-3 py-2 outline-none resize-none"
                        style={{
                          color: 'var(--text-primary)',
                          background: 'var(--alpha-white-3)',
                          'border-color': 'rgba(239, 68, 68, 0.4)',
                        }}
                      />
                      <div class="flex items-center justify-between mt-2">
                        <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                          Enter to send, Esc to cancel
                        </span>
                        <div class="flex items-center gap-2">
                          <button
                            type="button"
                            onClick={() => {
                              setShowRejectInput(false)
                              setRejectFeedback('')
                            }}
                            class="text-[12px] px-3 py-1 rounded-md transition-colors"
                            style={{ color: 'var(--text-muted)' }}
                          >
                            Cancel
                          </button>
                          <button
                            type="button"
                            onClick={handleReject}
                            disabled={!rejectFeedback().trim()}
                            class="text-[12px] px-3 py-1 rounded-md font-medium transition-all"
                            style={{
                              color: rejectFeedback().trim() ? '#EF4444' : 'var(--text-muted)',
                              background: rejectFeedback().trim()
                                ? 'rgba(239, 68, 68, 0.1)'
                                : 'transparent',
                              opacity: rejectFeedback().trim() ? '1' : '0.5',
                            }}
                          >
                            Send Rejection
                          </button>
                        </div>
                      </div>
                    </div>
                  </Show>

                  {/* Keyboard shortcut hints */}
                  <div
                    class="flex items-center gap-3 mb-3 text-[10px]"
                    style={{ color: 'var(--text-muted)' }}
                  >
                    <span class="flex items-center gap-1">
                      <kbd
                        class="px-1.5 py-0.5 rounded border"
                        style={{
                          background: 'var(--alpha-white-5)',
                          'border-color': 'var(--border-subtle)',
                        }}
                      >
                        Ctrl+Enter
                      </kbd>{' '}
                      Execute
                    </span>
                    <span style={{ opacity: '0.4' }}>&bull;</span>
                    <span class="flex items-center gap-1">
                      <kbd
                        class="px-1.5 py-0.5 rounded border"
                        style={{
                          background: 'var(--alpha-white-5)',
                          'border-color': 'var(--border-subtle)',
                        }}
                      >
                        Ctrl+S
                      </kbd>{' '}
                      Download
                    </span>
                    <span style={{ opacity: '0.4' }}>&bull;</span>
                    <span class="flex items-center gap-1">
                      <kbd
                        class="px-1.5 py-0.5 rounded border"
                        style={{
                          background: 'var(--alpha-white-5)',
                          'border-color': 'var(--border-subtle)',
                        }}
                      >
                        Esc
                      </kbd>{' '}
                      Close
                    </span>
                  </div>

                  <div class="flex items-center justify-between">
                    {/* Left: Reject + Refine + comment count */}
                    <div class="flex items-center gap-2">
                      {/* Reject — red text button */}
                      <button
                        type="button"
                        onClick={() => setShowRejectInput(true)}
                        class="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-md text-[12px] font-medium transition-all hover:opacity-90"
                        style={{
                          color: '#EF4444',
                          background: 'transparent',
                        }}
                        title="Reject the plan with feedback"
                      >
                        <XCircle class="w-3.5 h-3.5" />
                        Reject
                      </button>

                      {/* Refine Plan — text button */}
                      <button
                        type="button"
                        onClick={handleRefine}
                        class="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-md text-[12px] font-medium transition-all hover:opacity-90"
                        style={{
                          color: 'var(--text-secondary)',
                          background: 'transparent',
                        }}
                        title="Send modifications back to the agent"
                      >
                        <PenLine class="w-3.5 h-3.5" />
                        Refine Plan
                      </button>

                      <Show when={commentCount() > 0}>
                        <span
                          class="inline-flex items-center gap-1 text-[11px]"
                          style={{ color: PLAN_ACCENT }}
                        >
                          <MessageSquare class="w-3 h-3" />
                          {commentCount()} comment{commentCount() !== 1 ? 's' : ''}
                        </span>
                      </Show>
                    </div>

                    {/* Right: Execute + Praxis */}
                    <div class="flex items-center gap-2">
                      {/* Execute Plan — primary filled button */}
                      <button
                        type="button"
                        onClick={() => handleApprove('code')}
                        class="inline-flex items-center gap-1.5 px-4 py-1.5 rounded-md text-[12px] font-semibold transition-all hover:opacity-90"
                        style={{
                          background: PLAN_ACCENT,
                          color: '#ffffff',
                          'box-shadow': `0 1px 3px ${PLAN_ACCENT_SUBTLE}`,
                        }}
                        title="Approve and execute plan in Code mode (Ctrl+Enter)"
                      >
                        <Play class="w-3.5 h-3.5" />
                        Execute Plan
                      </button>

                      {/* Start with Praxis — secondary outlined button */}
                      <button
                        type="button"
                        onClick={() => handleApprove('praxis')}
                        class="inline-flex items-center gap-1.5 px-3.5 py-1.5 rounded-md text-[12px] font-medium border transition-all hover:opacity-90"
                        style={{
                          color: PLAN_ACCENT,
                          'border-color': PLAN_ACCENT,
                          background: 'transparent',
                        }}
                        title="Approve and execute plan with Praxis multi-agent mode (Ctrl+Shift+Enter)"
                      >
                        <Users class="w-3.5 h-3.5" />
                        Start with Praxis
                      </button>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      )}
    </Show>
  )
}

export default PlanOverlay
