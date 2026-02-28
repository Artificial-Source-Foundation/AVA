/**
 * Planning pipeline — structured task decomposition.
 *
 * The Planner agent returns a TaskPlan, which the Commander
 * uses to delegate subtasks to the appropriate leads.
 */

export interface SubtaskPlan {
  description: string
  /** Domain: frontend, backend, testing, devops, fullstack */
  domain: string
  /** Files likely affected */
  files: string[]
  /** Suggested lead agent ID to handle this subtask */
  assignTo: string
}

export interface TaskPlan {
  subtasks: SubtaskPlan[]
  /** Dependency pairs: [blocker, blocked] index pairs */
  dependencies: Array<[number, number]>
}

/**
 * Parse a TaskPlan from the planner agent's output.
 * The planner returns JSON embedded in its text response.
 */
export function parseTaskPlan(output: string): TaskPlan | null {
  // Try to extract JSON from the output
  const jsonMatch = output.match(/\{[\s\S]*"subtasks"[\s\S]*\}/)
  if (!jsonMatch) return null

  try {
    const parsed = JSON.parse(jsonMatch[0]) as TaskPlan
    if (!Array.isArray(parsed.subtasks)) return null

    // Validate subtask structure
    const valid = parsed.subtasks.every(
      (s) =>
        typeof s.description === 'string' &&
        typeof s.domain === 'string' &&
        Array.isArray(s.files) &&
        typeof s.assignTo === 'string'
    )
    if (!valid) return null

    return {
      subtasks: parsed.subtasks,
      dependencies: Array.isArray(parsed.dependencies) ? parsed.dependencies : [],
    }
  } catch {
    return null
  }
}

/**
 * Order subtasks respecting dependencies.
 * Returns indices in execution order (simple topological sort).
 */
export function orderSubtasks(plan: TaskPlan): number[] {
  const n = plan.subtasks.length
  const inDegree = Array.from<number>({ length: n }).fill(0)
  const adj = new Map<number, number[]>()

  for (const [from, to] of plan.dependencies) {
    if (from >= 0 && from < n && to >= 0 && to < n) {
      inDegree[to]++
      const edges = adj.get(from) ?? []
      edges.push(to)
      adj.set(from, edges)
    }
  }

  // BFS topological sort
  const queue: number[] = []
  for (let i = 0; i < n; i++) {
    if (inDegree[i] === 0) queue.push(i)
  }

  const order: number[] = []
  while (queue.length > 0) {
    const node = queue.shift()!
    order.push(node)
    for (const next of adj.get(node) ?? []) {
      inDegree[next]--
      if (inDegree[next] === 0) queue.push(next)
    }
  }

  // If cycle detected, just return all indices in original order
  if (order.length < n) {
    return Array.from({ length: n }, (_, i) => i)
  }

  return order
}

/**
 * Format a TaskPlan into a readable summary for the Commander.
 */
export function formatPlanSummary(plan: TaskPlan): string {
  const lines = [`**Task Plan** (${plan.subtasks.length} subtasks):\n`]

  for (let i = 0; i < plan.subtasks.length; i++) {
    const s = plan.subtasks[i]
    const deps = plan.dependencies
      .filter(([_, to]) => to === i)
      .map(([from]) => `#${from + 1}`)
      .join(', ')
    const depStr = deps ? ` (after ${deps})` : ''
    lines.push(`${i + 1}. [${s.domain}] ${s.description} → \`delegate_${s.assignTo}\`${depStr}`)
    if (s.files.length > 0) {
      lines.push(`   Files: ${s.files.join(', ')}`)
    }
  }

  return lines.join('\n')
}
