/**
 * Agent Evaluator
 * Evaluates goal completion and progress tracking
 */

import type { AgentResult, AgentStep } from './types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Progress status levels
 */
export type ProgressStatus = 'not_started' | 'in_progress' | 'blocked' | 'completed' | 'failed'

/**
 * Progress report for an agent's execution
 */
export interface ProgressReport {
  /** Overall status */
  status: ProgressStatus
  /** Percentage complete (0-100) */
  percentComplete: number
  /** Number of steps completed */
  stepsCompleted: number
  /** Total steps executed (including failed) */
  stepsExecuted: number
  /** Current step being executed */
  currentStep?: AgentStep
  /** Estimated time remaining in ms (if calculable) */
  estimatedTimeRemaining?: number
  /** Key milestones reached */
  milestones: string[]
  /** Current blockers if any */
  blockers: string[]
  /** Summary of progress */
  summary: string
}

/**
 * Goal evaluation result
 */
export interface GoalEvaluation {
  /** Whether the goal appears to be achieved */
  achieved: boolean
  /** Confidence level (0-1) */
  confidence: number
  /** Evidence supporting the evaluation */
  evidence: string[]
  /** Missing requirements if not achieved */
  missingRequirements: string[]
  /** Suggestions for completion */
  suggestions: string[]
}

// ============================================================================
// Progress Evaluation
// ============================================================================

/**
 * Calculate progress from completed steps
 */
export function calculateProgress(steps: AgentStep[], totalExpectedSteps?: number): ProgressReport {
  if (steps.length === 0) {
    return {
      status: 'not_started',
      percentComplete: 0,
      stepsCompleted: 0,
      stepsExecuted: 0,
      milestones: [],
      blockers: [],
      summary: 'Agent has not started execution',
    }
  }

  const completed = steps.filter((s) => s.status === 'success')
  const failed = steps.filter((s) => s.status === 'failed')
  const running = steps.find((s) => s.status === 'running')

  const stepsCompleted = completed.length
  const stepsExecuted = steps.length
  const total = totalExpectedSteps ?? Math.max(stepsExecuted, stepsCompleted + 1)

  // Calculate percentage
  const percentComplete = Math.round((stepsCompleted / total) * 100)

  // Determine status
  let status: ProgressStatus
  if (failed.length > 0 && failed.length === stepsExecuted) {
    status = 'failed'
  } else if (running) {
    status = 'in_progress'
  } else if (stepsCompleted === total) {
    status = 'completed'
  } else if (failed.length > 0) {
    status = 'blocked'
  } else {
    status = 'in_progress'
  }

  // Extract milestones from successful steps
  const milestones = completed
    .filter((s) => s.toolsCalled.length > 0)
    .map((s) => s.description.slice(0, 50))

  // Extract blockers from failed steps
  const blockers = failed.filter((s) => s.error).map((s) => s.error!)

  // Generate summary
  const summary = generateProgressSummary(status, stepsCompleted, stepsExecuted, failed.length)

  return {
    status,
    percentComplete,
    stepsCompleted,
    stepsExecuted,
    currentStep: running,
    milestones,
    blockers,
    summary,
  }
}

/**
 * Generate a human-readable progress summary
 */
function generateProgressSummary(
  status: ProgressStatus,
  completed: number,
  executed: number,
  failed: number
): string {
  switch (status) {
    case 'not_started':
      return 'Waiting to start execution'
    case 'in_progress':
      return `Executing... ${completed}/${executed} steps completed`
    case 'blocked':
      return `Blocked: ${failed} step(s) failed, ${completed} succeeded`
    case 'completed':
      return `Completed: All ${completed} steps successful`
    case 'failed':
      return `Failed: ${failed} of ${executed} steps failed`
    default:
      return 'Unknown status'
  }
}

// ============================================================================
// Goal Evaluation
// ============================================================================

/**
 * Evaluate whether a goal has been achieved based on execution results
 */
export function evaluateGoal(_goal: string, result: AgentResult): GoalEvaluation {
  const evidence: string[] = []
  const missingRequirements: string[] = []
  const suggestions: string[] = []

  // Check termination mode
  if (result.terminateMode === 'GOAL') {
    evidence.push('Agent signaled goal completion via attempt_completion')
  }

  // Check if we have successful steps
  const successfulSteps = result.steps.filter((s) => s.status === 'success')
  if (successfulSteps.length > 0) {
    evidence.push(`${successfulSteps.length} steps completed successfully`)
  }

  // Check for tool usage
  const toolsUsed = new Set<string>()
  for (const step of result.steps) {
    for (const tool of step.toolsCalled) {
      if (tool.success) {
        toolsUsed.add(tool.name)
      }
    }
  }
  if (toolsUsed.size > 0) {
    evidence.push(`Used ${toolsUsed.size} different tools: ${Array.from(toolsUsed).join(', ')}`)
  }

  // Check for failures
  const failedSteps = result.steps.filter((s) => s.status === 'failed')
  if (failedSteps.length > 0) {
    missingRequirements.push(`${failedSteps.length} steps failed`)
    for (const step of failedSteps) {
      if (step.error) {
        missingRequirements.push(`Step "${step.description}": ${step.error}`)
      }
    }
  }

  // Check output quality
  if (result.output && result.output.length > 0) {
    if (result.output.length > 100) {
      evidence.push('Agent provided detailed output')
    } else if (result.output.length > 10) {
      evidence.push('Agent provided output')
    }
  } else {
    missingRequirements.push('No output provided')
    suggestions.push('Ensure attempt_completion is called with a result')
  }

  // Calculate confidence
  let confidence = 0
  if (result.success) {
    confidence = 0.8 // Base confidence for successful completion
    if (successfulSteps.length > 2) confidence += 0.1
    if (result.output && result.output.length > 100) confidence += 0.1
  } else {
    confidence = 0.2 // Low confidence for failed execution
    if (successfulSteps.length > 0)
      confidence += 0.2 * (successfulSteps.length / result.steps.length)
  }
  confidence = Math.min(1, Math.max(0, confidence))

  // Generate suggestions based on issues
  if (result.turns >= result.steps.length && !result.success) {
    suggestions.push('Consider breaking the task into smaller sub-goals')
  }
  if (failedSteps.length > 0) {
    suggestions.push('Review failed steps and consider alternative approaches')
  }

  return {
    achieved: result.success,
    confidence,
    evidence,
    missingRequirements,
    suggestions,
  }
}

// ============================================================================
// Step Analysis
// ============================================================================

/**
 * Analyze tool usage patterns across steps
 */
export function analyzeToolUsage(steps: AgentStep[]): Map<
  string,
  {
    count: number
    successRate: number
    avgDuration: number
  }
> {
  const toolStats = new Map<
    string,
    {
      totalCalls: number
      successfulCalls: number
      totalDuration: number
    }
  >()

  for (const step of steps) {
    for (const tool of step.toolsCalled) {
      const stats = toolStats.get(tool.name) ?? {
        totalCalls: 0,
        successfulCalls: 0,
        totalDuration: 0,
      }

      stats.totalCalls++
      if (tool.success) stats.successfulCalls++
      if (tool.durationMs) stats.totalDuration += tool.durationMs

      toolStats.set(tool.name, stats)
    }
  }

  const result = new Map<
    string,
    {
      count: number
      successRate: number
      avgDuration: number
    }
  >()

  for (const [name, stats] of toolStats) {
    result.set(name, {
      count: stats.totalCalls,
      successRate: stats.totalCalls > 0 ? stats.successfulCalls / stats.totalCalls : 0,
      avgDuration: stats.totalCalls > 0 ? stats.totalDuration / stats.totalCalls : 0,
    })
  }

  return result
}

/**
 * Calculate execution metrics from an agent result
 */
export function calculateMetrics(result: AgentResult): {
  totalDuration: number
  avgStepDuration: number
  toolCallsPerTurn: number
  successRate: number
  tokensPerTurn: number
} {
  const totalSteps = result.steps.length
  const successfulSteps = result.steps.filter((s) => s.status === 'success').length

  let totalToolCalls = 0
  let totalStepDuration = 0

  for (const step of result.steps) {
    totalToolCalls += step.toolsCalled.length
    if (step.startedAt && step.completedAt) {
      totalStepDuration += step.completedAt - step.startedAt
    }
  }

  return {
    totalDuration: result.durationMs,
    avgStepDuration: totalSteps > 0 ? totalStepDuration / totalSteps : 0,
    toolCallsPerTurn: result.turns > 0 ? totalToolCalls / result.turns : 0,
    successRate: totalSteps > 0 ? successfulSteps / totalSteps : 0,
    tokensPerTurn: result.turns > 0 ? result.tokensUsed / result.turns : 0,
  }
}
