/**
 * Plan Dock Component
 *
 * Inline dock that sits between MessageList and MessageInput.
 * Displays when the agent proposes a plan (plan_created event).
 * Wraps PlanCard with agent hook integration for approve/reject/edit flows.
 */

import { type Component, Show } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { logInfo } from '../../services/logger'
import { useLayout } from '../../stores/layout'
import { usePlanOverlay } from '../../stores/planOverlayStore'
import type { PlanData } from '../../types/rust-ipc'
import { PlanCard } from './PlanCard'

export const PlanDock: Component = () => {
  const agent = useAgent()
  const { openPlan } = usePlanOverlay()
  const { openPlanViewer } = useLayout()

  const handleApprove = async (
    plan: PlanData,
    stepComments: Record<string, string>
  ): Promise<void> => {
    logInfo('plan', 'Plan approved', { steps: plan.steps.length })
    try {
      await agent.resolvePlan('approved', plan, undefined, stepComments)
    } catch {
      // Error logged by agent, dock remains consistent for retry
    }
  }

  const handleReject = async (
    feedback: string,
    stepComments: Record<string, string>
  ): Promise<void> => {
    logInfo('plan', 'Plan rejected', { feedback: feedback.slice(0, 80) })
    try {
      await agent.resolvePlan('rejected', undefined, feedback, stepComments)
    } catch {
      // Error logged by agent, dock remains consistent for retry
    }
  }

  const handleEdit = async (
    plan: PlanData,
    stepComments: Record<string, string>
  ): Promise<void> => {
    logInfo('plan', 'Plan modified', { steps: plan.steps.length })
    try {
      await agent.resolvePlan('modified', plan, undefined, stepComments)
    } catch {
      // Error logged by agent, dock remains consistent for retry
    }
  }

  const handleViewFull = (): void => {
    const plan = agent.pendingPlan()
    if (plan) {
      openPlan(plan)
      openPlanViewer('active')
    }
  }

  return (
    <Show when={agent.pendingPlan()}>
      {(plan) => (
        <div
          class="border-t border-b border-[var(--border-subtle)] bg-[var(--surface-raised)] px-4 py-3"
          style={{ animation: 'approvalSlideUp 150ms ease-out' }}
        >
          <PlanCard
            plan={plan()}
            onApprove={handleApprove}
            onReject={handleReject}
            onEdit={handleEdit}
            onViewFull={handleViewFull}
          />
        </div>
      )}
    </Show>
  )
}

export default PlanDock
