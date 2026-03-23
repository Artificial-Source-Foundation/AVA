import type { PlanData, PlanStep } from '../types/rust-ipc'

export type StepDiffType = 'added' | 'removed' | 'modified' | 'unchanged'

export interface StepDiff {
  step: PlanStep
  oldStep?: PlanStep
  diffType: StepDiffType
}

export interface PlanDiffResult {
  steps: StepDiff[]
  stats: { added: number; removed: number; modified: number }
}

export function diffPlans(oldPlan: PlanData, newPlan: PlanData): PlanDiffResult {
  const oldStepMap = new Map(oldPlan.steps.map((s) => [s.id, s]))
  const newStepMap = new Map(newPlan.steps.map((s) => [s.id, s]))

  const steps: StepDiff[] = []
  let added = 0
  let removed = 0
  let modified = 0

  // Process new steps
  for (const step of newPlan.steps) {
    const oldStep = oldStepMap.get(step.id)
    if (!oldStep) {
      steps.push({ step, diffType: 'added' })
      added++
    } else if (
      oldStep.description !== step.description ||
      oldStep.action !== step.action ||
      JSON.stringify(oldStep.files) !== JSON.stringify(step.files)
    ) {
      steps.push({ step, oldStep, diffType: 'modified' })
      modified++
    } else {
      steps.push({ step, oldStep, diffType: 'unchanged' })
    }
  }

  // Find removed steps
  for (const step of oldPlan.steps) {
    if (!newStepMap.has(step.id)) {
      steps.push({ step, diffType: 'removed' })
      removed++
    }
  }

  return { steps, stats: { added, removed, modified } }
}
