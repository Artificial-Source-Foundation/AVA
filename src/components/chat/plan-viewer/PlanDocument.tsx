import { Check, FileCode, GitBranch } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { PlanAnnotation } from '../../../stores/planOverlayStore'
import type { PlanData, PlanStep, PlanStepAction } from '../../../types/rust-ipc'
import { ACTION_CONFIG, PLAN_ACCENT, PLAN_ACCENT_SUBTLE } from './types'

/** Format a plan as markdown text for copy/download */
export function formatPlanMarkdown(plan: PlanData): string {
  return [
    `# ${plan.codename ? `${plan.codename} \u2014 ` : ''}${plan.summary}`,
    '',
    ...plan.steps.map(
      (s, i) =>
        `${i + 1}. [${s.action.toUpperCase()}] ${s.description}${s.files.length ? `\n   Files: ${s.files.join(', ')}` : ''}`
    ),
  ].join('\n')
}

/** Parse a plan markdown file back into PlanData */
export function parsePlanMarkdown(content: string): PlanData | null {
  const lines = content.split('\n')
  let codename: string | undefined
  let summary = ''
  const steps: PlanStep[] = []

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

    const stepMatch = line.match(/^(?:###\s+|(\d+)\.\s+)\[(\w+)]\s+(.+)/)
    if (stepMatch) {
      const action = (stepMatch[2].toLowerCase() as PlanStepAction) || 'implement'
      steps.push({
        id: `step-${steps.length + 1}`,
        description: stepMatch[3],
        files: [],
        action: (['research', 'implement', 'test', 'review'] as const).includes(
          action as PlanStepAction
        )
          ? action
          : 'implement',
        dependsOn: [],
        approved: false,
      })
    }
    const filesMatch = line.match(/^\s+Files:\s*(.+)/)
    if (filesMatch && steps.length > 0) {
      steps[steps.length - 1].files = filesMatch[1].split(',').map((f) => f.trim())
    }
  }

  if (!summary && steps.length === 0) return null
  return { summary, steps, estimatedTurns: steps.length, codename }
}

export const PlanDocument: Component<{
  plan: PlanData
  annotations: PlanAnnotation[]
  onMouseUp: (e: MouseEvent) => void
  onGlobalComment: () => void
  onCopyPlan: () => void
  cardRef: (el: HTMLElement) => void
}> = (props) => {
  const approvedCount = () => props.plan.steps.filter((s) => s.approved).length
  const estimatedCost = () =>
    props.plan.estimatedBudgetUsd != null ? `~$${props.plan.estimatedBudgetUsd.toFixed(2)}` : null

  return (
    <div class="flex justify-center px-6 py-8 plan-grid-bg min-h-full">
      <article
        ref={props.cardRef}
        onMouseUp={(e) => props.onMouseUp(e)}
        class="relative select-text w-full rounded-xl border"
        style={{
          'max-width': '1040px',
          background: 'var(--surface)',
          'border-color': 'var(--border-subtle)',
          'box-shadow': '0 8px 32px -8px rgba(0,0,0,0.2), 0 4px 12px -4px rgba(0,0,0,0.1)',
          padding: 'clamp(20px, 4vw, 40px)',
          animation: 'planCardIn 250ms ease-out',
        }}
      >
        {/* Top-right action links */}
        <div class="absolute top-4 right-4 flex items-center gap-3">
          <button
            type="button"
            onClick={() => props.onGlobalComment()}
            class="text-[11px] font-medium transition-colors"
            style={{ color: 'var(--text-muted)' }}
          >
            Global comment
          </button>
          <button
            type="button"
            onClick={() => props.onCopyPlan()}
            class="text-[11px] font-medium transition-colors"
            style={{ color: 'var(--text-muted)' }}
          >
            Copy plan
          </button>
        </div>

        {/* Codename badge */}
        <Show when={props.plan.codename}>
          <span
            class="inline-flex items-center px-2 py-0.5 rounded-full text-[10px] font-semibold tracking-wider uppercase mb-3"
            style={{ background: PLAN_ACCENT_SUBTLE, color: PLAN_ACCENT }}
          >
            {props.plan.codename}
          </span>
        </Show>

        {/* Title */}
        <h1
          class="text-[24px] font-bold leading-tight mb-3"
          style={{ color: 'var(--text-primary)' }}
        >
          {props.plan.summary}
        </h1>

        {/* Meta line */}
        <p class="text-[13px] mb-6" style={{ color: 'var(--text-muted)' }}>
          {props.plan.steps.length} steps
          {' \u00B7 '}~{props.plan.estimatedTurns} turns
          <Show when={estimatedCost()}>
            {' \u00B7 '}
            {estimatedCost()}
          </Show>
          <Show when={approvedCount() > 0}>
            {' \u00B7 '}
            <span style={{ color: '#22C55E' }}>
              {approvedCount()}/{props.plan.steps.length} approved
            </span>
          </Show>
        </p>

        {/* Divider */}
        <div class="mb-6" style={{ height: '1px', background: 'var(--border-subtle)' }} />

        {/* Steps rendered as document sections */}
        <div class="space-y-6">
          <For each={props.plan.steps}>
            {(step, i) => {
              const action = () => ACTION_CONFIG[step.action]
              const depLabels = () =>
                step.dependsOn.map((depId) => {
                  const idx = props.plan.steps.findIndex((s) => s.id === depId)
                  return idx >= 0 ? `Step ${idx + 1}` : depId
                })

              return (
                <section id={`plan-step-${step.id}`} data-step-id={step.id}>
                  {/* Step heading */}
                  <div class="flex items-center gap-3 mb-2">
                    <h3 class="text-[16px] font-semibold" style={{ color: 'var(--text-primary)' }}>
                      <span style={{ color: 'var(--text-muted)' }}>{i() + 1}.</span>{' '}
                      {step.description}
                    </h3>
                    <span
                      class="inline-flex items-center px-2 py-0.5 rounded-full text-[9px] font-semibold tracking-wider uppercase border flex-shrink-0"
                      style={{
                        background: action().bg,
                        color: action().text,
                        'border-color': action().border,
                      }}
                    >
                      {action().label}
                    </span>
                    <Show when={step.approved}>
                      <Check class="w-4 h-4 flex-shrink-0" style={{ color: '#22C55E' }} />
                    </Show>
                  </div>

                  {/* Files */}
                  <Show when={step.files.length > 0}>
                    <ul class="mb-2 space-y-0.5">
                      <For each={step.files}>
                        {(file) => (
                          <li class="flex items-center gap-2 text-[12px]">
                            <FileCode
                              class="w-3 h-3 flex-shrink-0"
                              style={{ color: 'var(--text-muted)' }}
                            />
                            <code
                              class="px-1 rounded"
                              style={{
                                color: 'var(--text-secondary)',
                                background: 'var(--alpha-white-3)',
                                'font-family': 'var(--font-ui-mono)',
                                'font-size': '11px',
                              }}
                            >
                              {file}
                            </code>
                          </li>
                        )}
                      </For>
                    </ul>
                  </Show>

                  {/* Dependencies */}
                  <Show when={step.dependsOn.length > 0}>
                    <p
                      class="flex items-center gap-1.5 text-[11px]"
                      style={{ color: 'var(--text-muted)' }}
                    >
                      <GitBranch class="w-3 h-3 flex-shrink-0" />
                      Depends on: {depLabels().join(', ')}
                    </p>
                  </Show>
                </section>
              )
            }}
          </For>
        </div>
      </article>
    </div>
  )
}
