import { ShieldCheck, Users } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'
import type { HqBoardReview, HqPhase, HqPlan } from '../../../types/hq'
import type { PlanData, PlanStep, PlanStepAction } from '../../../types/rust-ipc'
import { PlanFullScreen } from '../../chat/plan-viewer/PlanFullScreen'

function mapDomainToAction(domain: string): PlanStepAction {
  if (domain === 'research') return 'research'
  if (domain === 'qa') return 'test'
  if (domain === 'debug') return 'review'
  return 'implement'
}

function mapHqPlanToPlanData(plan: HqPlan): PlanData {
  const steps: PlanStep[] = plan.phases.flatMap((phase) =>
    phase.tasks.map((task) => ({
      id: task.id,
      description: `${phase.name}: ${task.title}`,
      files: task.fileHints,
      action: mapDomainToAction(task.domain),
      dependsOn: task.dependencies,
      approved: plan.status === 'approved' || plan.status === 'executing',
    }))
  )

  const estimatedTurns = plan.phases.reduce(
    (sum, phase) => sum + phase.tasks.reduce((taskSum, task) => taskSum + task.budgetMaxTurns, 0),
    0
  )
  const estimatedBudgetUsd = plan.phases.reduce(
    (sum, phase) => sum + phase.tasks.reduce((taskSum, task) => taskSum + task.budgetMaxCostUsd, 0),
    0
  )

  return {
    summary: plan.directorDescription,
    steps,
    estimatedTurns,
    estimatedBudgetUsd,
    codename: plan.title,
  }
}

const HqPlanReview: Component = () => {
  const { plan, approveCurrentPlan, rejectCurrentPlan, navigateTo } = useHq()

  const currentPlan = createMemo(() => plan())
  const planData = createMemo(() => (currentPlan() ? mapHqPlanToPlanData(currentPlan()!) : null))

  return (
    <Show
      when={currentPlan() && planData()}
      fallback={
        <div class="flex h-full items-center justify-center" style={{ color: 'var(--text-muted)' }}>
          No plan is ready for review yet.
        </div>
      }
    >
      <PlanFullScreen
        plan={planData()!}
        onClose={() => navigateTo('director-chat', 'Director Chat')}
        onApprove={() => void approveCurrentPlan()}
        onRevise={() => void rejectCurrentPlan('Please revise the plan and return with updates.')}
        sidebarLabel="HQ Review"
        sidebarTop={
          <>
            <BoardSection boardReview={currentPlan()?.boardReview} />
            <QaSection phases={currentPlan()?.phases ?? []} />
          </>
        }
        sidebarBottom={<RevisionNotes />}
      />
    </Show>
  )
}

const BoardSection: Component<{ boardReview?: HqBoardReview }> = (props) => (
  <section
    class="rounded-xl border p-3"
    style={{ 'border-color': 'var(--border-subtle)', 'background-color': 'rgba(255,255,255,0.02)' }}
  >
    <div
      class="text-[10px] font-semibold uppercase tracking-[0.18em]"
      style={{ color: 'var(--text-muted)' }}
    >
      Board of Directors
    </div>
    <Show
      when={props.boardReview}
      fallback={
        <div class="mt-2 text-[11px] leading-relaxed" style={{ color: 'var(--text-muted)' }}>
          This plan is staying on the Director path. No board vote is required right now.
        </div>
      }
    >
      {(review) => (
        <>
          <div class="mt-2 text-[12px] font-semibold" style={{ color: 'var(--text-primary)' }}>
            Board consultation complete
          </div>
          <div class="mt-1 text-[11px] leading-relaxed" style={{ color: 'var(--text-muted)' }}>
            {review().consensus}
          </div>
          <div class="mt-2 text-[10px] font-semibold" style={{ color: 'var(--accent)' }}>
            {review().voteSummary}
          </div>
          <div class="mt-3 flex flex-col gap-2">
            <For each={review().opinions}>
              {(opinion) => (
                <div
                  class="rounded-lg px-2.5 py-2"
                  style={{ 'background-color': 'rgba(255,255,255,0.03)' }}
                >
                  <div class="flex items-center justify-between gap-2">
                    <span class="text-[11px]" style={{ color: 'var(--text-primary)' }}>
                      {opinion.memberName}
                    </span>
                    <span
                      class="text-[10px] font-semibold"
                      style={{
                        color:
                          opinion.vote.toLowerCase() === 'approve'
                            ? 'var(--success)'
                            : opinion.vote.toLowerCase() === 'amend'
                              ? 'var(--warning)'
                              : 'var(--error)',
                      }}
                    >
                      {opinion.vote}
                    </span>
                  </div>
                  <div
                    class="mt-1 text-[10px] leading-relaxed"
                    style={{ color: 'var(--text-muted)' }}
                  >
                    {opinion.recommendation}
                  </div>
                </div>
              )}
            </For>
          </div>
        </>
      )}
    </Show>
  </section>
)

const QaSection: Component<{ phases: HqPhase[] }> = (props) => (
  <section
    class="rounded-xl border p-3"
    style={{ 'border-color': 'rgba(52,199,89,0.16)', 'background-color': 'rgba(52,199,89,0.07)' }}
  >
    <div
      class="flex items-center gap-2 text-[10px] font-semibold uppercase tracking-[0.18em]"
      style={{ color: 'var(--text-muted)' }}
    >
      <ShieldCheck size={12} />
      QA checkpoints
    </div>
    <div class="mt-3 flex flex-col gap-2">
      <For each={props.phases.filter((phase) => phase.reviewEnabled)}>
        {(phase) => (
          <div class="rounded-lg px-2.5 py-2" style={{ 'background-color': 'rgba(0,0,0,0.12)' }}>
            <div class="text-[11px] font-semibold" style={{ color: 'var(--text-primary)' }}>
              {phase.name}
            </div>
            <div class="mt-1 text-[10px] leading-relaxed" style={{ color: 'var(--text-muted)' }}>
              Review gate owned by {phase.reviewAssignee || 'QA Lead'} before the next phase opens.
            </div>
          </div>
        )}
      </For>
    </div>
  </section>
)

const RevisionNotes: Component = () => (
  <section
    class="rounded-xl border p-3"
    style={{ 'border-color': 'var(--border-subtle)', 'background-color': 'rgba(255,255,255,0.02)' }}
  >
    <div
      class="flex items-center gap-2 text-[10px] font-semibold uppercase tracking-[0.18em]"
      style={{ color: 'var(--text-muted)' }}
    >
      <Users size={12} />
      Revision notes
    </div>
    <div class="mt-2 text-[11px] leading-relaxed" style={{ color: 'var(--text-muted)' }}>
      Use “Send for Revisions” when scope changed, sequencing feels risky, or QA gates need to move.
      The Director should return with an updated plan instead of patching silently mid-flight.
    </div>
  </section>
)

export default HqPlanReview
