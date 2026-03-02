/**
 * Recipe runner — executes recipe workflows step by step.
 *
 * Supports sequential and parallel execution, step result substitution,
 * and condition evaluation.
 */

import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { substituteParams, substituteStepResults } from './parser.js'
import type { Recipe, RecipeResult } from './types.js'

interface StepResult {
  name: string
  success: boolean
  result?: string
  error?: string
  duration: number
}

/**
 * Execute a recipe with the given parameters.
 *
 * Steps run sequentially by default. Consecutive steps with `parallel: true`
 * are grouped and run concurrently via Promise.all.
 */
export async function executeRecipe(
  recipe: Recipe,
  params: Record<string, string>,
  api: ExtensionAPI
): Promise<RecipeResult> {
  const startedAt = Date.now()
  const resolved = substituteParams(recipe, params)
  const stepResults = new Map<string, string>()
  const results: StepResult[] = []

  // Group steps: consecutive parallel steps form a batch
  const groups = groupSteps(resolved.steps)

  for (const group of groups) {
    if (group.parallel) {
      const batchResults = await Promise.all(
        group.steps.map((step) => executeSingleStep(step, stepResults, api))
      )
      for (const r of batchResults) {
        results.push(r)
        if (r.success && r.result) {
          stepResults.set(r.name, r.result)
        }
      }
    } else {
      for (const step of group.steps) {
        const r = await executeSingleStep(step, stepResults, api)
        results.push(r)
        if (r.success && r.result) {
          stepResults.set(r.name, r.result)
        }
      }
    }
  }

  const completedAt = Date.now()
  return {
    recipe: recipe.name,
    startedAt,
    completedAt,
    steps: results,
    success: results.every((r) => r.success),
  }
}

// ─── Step Groups ─────────────────────────────────────────────────────────────

interface StepGroup {
  parallel: boolean
  steps: Array<{
    name: string
    tool?: string
    command?: string
    goal?: string
    args?: Record<string, string>
    condition?: string
  }>
}

function groupSteps(
  steps: Array<{
    name: string
    tool?: string
    command?: string
    goal?: string
    args?: Record<string, string>
    parallel?: boolean
    condition?: string
  }>
): StepGroup[] {
  const groups: StepGroup[] = []
  let currentGroup: StepGroup | undefined

  for (const step of steps) {
    const isParallel = step.parallel === true
    if (!currentGroup || currentGroup.parallel !== isParallel) {
      currentGroup = { parallel: isParallel, steps: [] }
      groups.push(currentGroup)
    }
    currentGroup.steps.push(step)
  }

  return groups
}

// ─── Single Step Execution ───────────────────────────────────────────────────

async function executeSingleStep(
  step: {
    name: string
    tool?: string
    command?: string
    goal?: string
    args?: Record<string, string>
    condition?: string
  },
  stepResults: Map<string, string>,
  api: ExtensionAPI
): Promise<StepResult> {
  const stepStart = Date.now()

  // Evaluate condition
  if (step.condition && !evaluateCondition(step.condition, stepResults)) {
    return {
      name: step.name,
      success: true,
      result: 'Skipped (condition not met)',
      duration: Date.now() - stepStart,
    }
  }

  // Substitute step result references in args
  const resolvedArgs = step.args ? substituteStepResults(step.args, stepResults) : {}

  try {
    let result: string | undefined

    if (step.tool) {
      result = await emitAndWaitForResult(api, 'recipe:execute-tool', {
        tool: step.tool,
        args: resolvedArgs,
        step: step.name,
      })
    } else if (step.command) {
      result = await emitAndWaitForResult(api, 'recipe:execute-command', {
        command: step.command,
        args: resolvedArgs,
        step: step.name,
      })
    } else if (step.goal) {
      // Substitute step results in the goal text too
      const resolvedGoal = step.goal.replace(
        /\{\{steps\.([^.}]+)\.result\}\}/g,
        (_match, stepName: string) => stepResults.get(stepName) ?? ''
      )
      result = await emitAndWaitForResult(api, 'recipe:execute-goal', {
        goal: resolvedGoal,
        args: resolvedArgs,
        step: step.name,
      })
    }

    return {
      name: step.name,
      success: true,
      result: result ?? 'completed',
      duration: Date.now() - stepStart,
    }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    return {
      name: step.name,
      success: false,
      error: message,
      duration: Date.now() - stepStart,
    }
  }
}

// ─── Condition Evaluation ────────────────────────────────────────────────────

/**
 * Evaluate a simple condition string.
 *
 * Supported forms:
 * - "steps.stepName.success" — true if step exists in results
 * - "steps.stepName.result" — true if step has a non-empty result
 */
export function evaluateCondition(condition: string, stepResults: Map<string, string>): boolean {
  const trimmed = condition.trim()

  // steps.X.success — step completed and is in results map
  const successMatch = trimmed.match(/^steps\.(\w+)\.success$/)
  if (successMatch) {
    return stepResults.has(successMatch[1]!)
  }

  // steps.X.result — step has a non-empty result
  const resultMatch = trimmed.match(/^steps\.(\w+)\.result$/)
  if (resultMatch) {
    const val = stepResults.get(resultMatch[1]!)
    return val !== undefined && val !== ''
  }

  // Fallback: treat as truthy check on a step name
  return stepResults.has(trimmed)
}

// ─── Event Helper ────────────────────────────────────────────────────────────

/**
 * Emit a recipe execution event and return a result string.
 *
 * The event handler is expected to be set up by the host (CLI or desktop app).
 * If no handler responds, returns a default result.
 */
async function emitAndWaitForResult(
  api: ExtensionAPI,
  event: string,
  data: Record<string, unknown>
): Promise<string> {
  // Use a promise-based event pattern: emit with a callback
  return new Promise<string>((resolve) => {
    const timeout = setTimeout(() => {
      resolve('completed (no handler)')
    }, 30_000)

    api.emit(event, {
      ...data,
      respond(result: string) {
        clearTimeout(timeout)
        resolve(result)
      },
    })
  })
}
