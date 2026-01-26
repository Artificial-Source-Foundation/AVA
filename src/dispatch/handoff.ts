/**
 * Delta9 Operator Handoff Builder
 *
 * Builds structured handoff contracts for operators.
 * Ensures operators have complete context without guessing.
 */

import type { Task, Mission } from '../types/mission.js'
import type { OperatorHandoff } from '../types/handoff.js'
import { DEFAULT_ESCALATION, DEFAULT_MUST_NOT } from '../types/handoff.js'

// =============================================================================
// Types
// =============================================================================

export interface HandoffBuilderInput {
  /** Task being assigned */
  task: Task
  /** Full mission context */
  mission: Mission
  /** All tasks for context */
  allTasks: Task[]
  /** Additional context from Commander */
  additionalContext?: string
}

// =============================================================================
// Builder
// =============================================================================

/**
 * Build a complete handoff contract for an operator.
 */
export function buildOperatorHandoff(input: HandoffBuilderInput): OperatorHandoff {
  const { task, mission, allTasks, additionalContext } = input

  // Build contract section
  const contract = {
    taskId: task.id,
    filesOwned: task.files ?? [],
    filesReadonly: task.filesReadonly ?? [],
    successCriteria: task.acceptanceCriteria ?? [],
    mustNot: [...DEFAULT_MUST_NOT, ...(task.mustNot ?? [])],
  }

  // Build context section
  const context = {
    missionSummary: buildMissionSummary(mission),
    yourRole: buildRoleDescription(task),
    priorWork: buildPriorWorkSummary(allTasks, task.id),
    nextSteps: buildNextStepsSummary(allTasks, task.id, additionalContext),
  }

  // Escalation section (use defaults, can be customized later)
  const escalation = { ...DEFAULT_ESCALATION }

  return { contract, context, escalation }
}

/**
 * Format handoff for injection into operator prompt.
 */
export function formatHandoffForPrompt(handoff: OperatorHandoff): string {
  const lines: string[] = []

  lines.push('# OPERATOR HANDOFF CONTRACT')
  lines.push('')

  // Contract section
  lines.push('## CONTRACT')
  lines.push(`Task ID: ${handoff.contract.taskId}`)
  lines.push('')

  if (handoff.contract.filesOwned.length > 0) {
    lines.push('### Files You Own (can modify)')
    for (const file of handoff.contract.filesOwned) {
      lines.push(`- ${file}`)
    }
    lines.push('')
  }

  if (handoff.contract.filesReadonly.length > 0) {
    lines.push('### Files Read-Only (do NOT modify)')
    for (const file of handoff.contract.filesReadonly) {
      lines.push(`- ${file}`)
    }
    lines.push('')
  }

  lines.push('### Success Criteria')
  for (const criterion of handoff.contract.successCriteria) {
    lines.push(`- [ ] ${criterion}`)
  }
  lines.push('')

  lines.push('### MUST NOT')
  for (const constraint of handoff.contract.mustNot) {
    lines.push(`- ${constraint}`)
  }
  lines.push('')

  // Context section
  lines.push('## CONTEXT')
  lines.push('')
  lines.push('### Mission Summary')
  lines.push(handoff.context.missionSummary)
  lines.push('')
  lines.push('### Your Role')
  lines.push(handoff.context.yourRole)
  lines.push('')
  lines.push('### Prior Work')
  lines.push(handoff.context.priorWork)
  lines.push('')
  lines.push('### Next Steps')
  lines.push(handoff.context.nextSteps)
  lines.push('')

  // Escalation section
  lines.push('## ESCALATION')
  lines.push('')
  lines.push(`**If Blocked:** ${handoff.escalation.blockedAction}`)
  lines.push('')
  lines.push(`**If Scope Change Needed:** ${handoff.escalation.scopeChangeAction}`)

  return lines.join('\n')
}

// =============================================================================
// Helpers
// =============================================================================

function buildMissionSummary(mission: Mission): string {
  const progress = calculateProgress(mission)
  return `${mission.description} (${mission.status}, ${progress}% complete)`
}

function buildRoleDescription(task: Task): string {
  return `Complete task "${task.description}" by meeting all success criteria. You are the assigned operator for this specific task.`
}

function buildPriorWorkSummary(allTasks: Task[], currentTaskId: string): string {
  const completedTasks = allTasks.filter((t) => t.status === 'completed' && t.id !== currentTaskId)

  if (completedTasks.length === 0) {
    return 'This is the first task. No prior work completed yet.'
  }

  const summaries = completedTasks.slice(-3).map((t) => {
    const files = t.filesChanged?.join(', ') || 'no files tracked'
    return `- ${t.description} (files: ${files})`
  })

  const prefix =
    completedTasks.length > 3
      ? `${completedTasks.length} tasks completed. Recent:\n`
      : 'Completed:\n'

  return prefix + summaries.join('\n')
}

function buildNextStepsSummary(
  allTasks: Task[],
  currentTaskId: string,
  additionalContext?: string
): string {
  const pendingTasks = allTasks.filter((t) => t.status === 'pending' && t.id !== currentTaskId)

  const parts: string[] = []

  if (pendingTasks.length > 0) {
    parts.push(
      `After this task, ${pendingTasks.length} more task(s) remain: ${pendingTasks
        .slice(0, 2)
        .map((t) => t.description)
        .join(', ')}${pendingTasks.length > 2 ? '...' : ''}`
    )
  } else {
    parts.push('This is the final task for the current objective.')
  }

  if (additionalContext) {
    parts.push(`Note: ${additionalContext}`)
  }

  return parts.join('\n')
}

function calculateProgress(mission: Mission): number {
  let total = 0
  let completed = 0

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      total++
      if (task.status === 'completed') {
        completed++
      }
    }
  }

  if (total === 0) return 0
  return Math.round((completed / total) * 100)
}
