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
import type { PlanData } from '../../types/rust-ipc'
import { PlanCard } from './PlanCard'

export const PlanDock: Component = () => {
  const agent = useAgent()

  const handleApprove = (plan: PlanData, stepComments: Record<string, string>): void => {
    logInfo('plan', 'Plan approved', { steps: plan.steps.length })
    agent.resolvePlan('approved', plan, undefined, stepComments)
  }

  const handleReject = (feedback: string, stepComments: Record<string, string>): void => {
    logInfo('plan', 'Plan rejected', { feedback: feedback.slice(0, 80) })
    agent.resolvePlan('rejected', undefined, feedback, stepComments)
  }

  const handleEdit = (plan: PlanData, stepComments: Record<string, string>): void => {
    logInfo('plan', 'Plan modified', { steps: plan.steps.length })
    agent.resolvePlan('modified', plan, undefined, stepComments)
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
          />
        </div>
      )}
    </Show>
  )
}

export default PlanDock
