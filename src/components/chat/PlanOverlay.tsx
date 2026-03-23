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
  ClipboardList,
  Clock,
  Copy,
  DollarSign,
  FileCode,
  GitBranch,
  Hash,
  X,
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
import { usePlanOverlay } from '../../stores/planOverlayStore'
import type { PlanStep, PlanStepAction } from '../../types/rust-ipc'

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

// ============================================================================
// Sub-components
// ============================================================================

/** Individual step rendered as a card with left accent border */
const StepCard: Component<{ step: PlanStep; index: number; allSteps: PlanStep[] }> = (props) => {
  const action = () => ACTION_CONFIG[props.step.action]
  const depLabels = () =>
    props.step.dependsOn.map((depId) => {
      const idx = props.allSteps.findIndex((s) => s.id === depId)
      return idx >= 0 ? `Step ${idx + 1}` : depId
    })

  return (
    <div
      class="rounded-lg border overflow-hidden transition-all duration-150"
      style={{
        background: 'var(--surface)',
        'border-color': props.step.approved ? 'rgba(34, 197, 94, 0.3)' : 'var(--border-subtle)',
        'border-left': `3px solid ${props.step.approved ? '#22C55E' : action().text}`,
      }}
    >
      {/* Step header */}
      <div class="flex items-center gap-3 px-4 py-3">
        {/* Step number / check */}
        <div
          class="w-7 h-7 rounded-full flex items-center justify-center text-[12px] font-bold flex-shrink-0 transition-colors"
          style={{
            background: props.step.approved ? 'rgba(34, 197, 94, 0.15)' : action().bg,
            color: props.step.approved ? '#22C55E' : action().text,
          }}
        >
          <Show when={props.step.approved} fallback={props.index + 1}>
            <Check class="w-4 h-4" />
          </Show>
        </div>

        {/* Description */}
        <span class="text-[14px] text-[var(--text-primary)] font-medium flex-1 leading-snug">
          {props.step.description}
        </span>

        {/* Action badge */}
        <span
          class="inline-flex items-center px-2.5 py-1 rounded-full text-[10px] font-semibold tracking-wider uppercase flex-shrink-0 border"
          style={{ background: action().bg, color: action().text, 'border-color': action().border }}
        >
          {action().label}
        </span>
      </div>

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
    </div>
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
  const { activePlan, isOpen, closePlan } = usePlanOverlay()
  const [copied, setCopied] = createSignal(false)

  // Esc key handler
  createEffect(() => {
    if (!isOpen()) return
    const handleKeyDown = (e: KeyboardEvent): void => {
      if (e.key === 'Escape') {
        e.preventDefault()
        e.stopPropagation()
        closePlan()
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
    const text = [
      `# ${plan.codename ? `${plan.codename} — ` : ''}${plan.summary}`,
      '',
      ...plan.steps.map(
        (s, i) =>
          `${i + 1}. [${s.action.toUpperCase()}] ${s.description}${s.files.length ? `\n   Files: ${s.files.join(', ')}` : ''}`
      ),
    ].join('\n')
    navigator.clipboard.writeText(text).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 2000)
    })
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
              </div>
            </div>

            {/* Right: Actions */}
            <div class="flex items-center gap-1">
              <button
                type="button"
                onClick={handleCopy}
                class="p-2 rounded-md transition-colors"
                style={{ color: 'var(--text-muted)' }}
                title="Copy plan as text"
              >
                <Show when={copied()} fallback={<Copy class="w-4 h-4" />}>
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

          {/* ── Canvas area with centered document card ── */}
          <div
            class="flex-1 overflow-y-auto"
            style={{
              background: `
                radial-gradient(circle at 50% 0%, ${PLAN_ACCENT_GLOW} 0%, transparent 50%),
                var(--bg)
              `,
              // Subtle grid pattern like Plannotator
              'background-size': '100% 100%, 24px 24px',
            }}
          >
            {/* Document card — centered, max-width, elevated */}
            <div
              class="mx-auto my-8 rounded-xl border overflow-hidden"
              style={{
                'max-width': '780px',
                background: 'var(--surface)',
                'border-color': 'var(--border-subtle)',
                'box-shadow': `
                  0 4px 6px -1px rgba(0, 0, 0, 0.15),
                  0 10px 15px -3px rgba(0, 0, 0, 0.12),
                  0 20px 40px -4px rgba(0, 0, 0, 0.08)
                `,
              }}
            >
              {/* ── Document header (inside card) ── */}
              <div
                class="px-8 pt-8 pb-6"
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
                    <MetaPill icon={DollarSign}>~${plan().estimatedBudgetUsd!.toFixed(2)}</MetaPill>
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
              <div class="px-8 py-6">
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
                  <For each={plan().steps}>
                    {(step, index) => (
                      <StepCard step={step} index={index()} allSteps={plan().steps} />
                    )}
                  </For>
                </div>
              </div>

              {/* ── Footer (inside card) ── */}
              <div
                class="px-8 py-4 flex items-center justify-between"
                style={{
                  'border-top': '1px solid var(--border-subtle)',
                  background: 'var(--surface-raised)',
                }}
              >
                <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                  Plan auto-saved to .ava/plans/
                </span>
                <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                  Press{' '}
                  <kbd
                    class="px-1.5 py-0.5 rounded text-[10px] border"
                    style={{
                      background: 'var(--alpha-white-5)',
                      'border-color': 'var(--border-subtle)',
                    }}
                  >
                    Esc
                  </kbd>{' '}
                  to close
                </span>
              </div>
            </div>
          </div>
        </div>
      )}
    </Show>
  )
}

export default PlanOverlay
