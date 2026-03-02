/**
 * Orchestrator — parallel execution of subtasks from a TaskPlan.
 *
 * Groups subtasks into dependency-ordered batches and executes
 * independent subtasks within each batch via Promise.all().
 * Respects a configurable concurrency limit.
 * Emits progress events: orchestration:batch-start, orchestration:batch-complete.
 */

import type { AgentResult } from '@ava/core-v2/agent'
import { createLogger } from '@ava/core-v2/logger'
import type { TaskPlan } from './planning.js'
import { getAgentsByTier } from './registry.js'
import { analyzeDomain } from './router.js'

const log = createLogger('Orchestrator')

export interface OrchestratorConfig {
  /** Maximum parallel delegations per batch. Default: 3 */
  maxParallelDelegations: number
  /** Retry failed subtasks within the orchestrator. Default: true */
  retryFailedSubtasks: boolean
  /** Max retries per subtask. Default: 1 */
  maxRetries: number
}

/** Callback for orchestration progress events. */
export type OrchestrationEventCallback = (event: Record<string, unknown>) => void

export interface SubtaskResult {
  subtaskId: string
  agentId: string
  success: boolean
  output: string
  durationMs: number
}

export interface OrchestrationResult {
  plan: TaskPlan
  results: SubtaskResult[]
  success: boolean
  summary: string
}

/**
 * Execute an orchestrated plan.
 * 1. Compute dependency batches (ready = all deps completed)
 * 2. Execute independent subtasks in parallel batches
 * 3. Retry failed subtasks if configured
 * 4. Emit batch-level progress events
 * 5. Aggregate results into a summary
 */
export async function executeOrchestration(
  plan: TaskPlan,
  delegateFn: (agentId: string, task: string) => Promise<AgentResult>,
  config?: Partial<OrchestratorConfig>,
  onEvent?: OrchestrationEventCallback
): Promise<OrchestrationResult> {
  const cfg: OrchestratorConfig = {
    maxParallelDelegations: config?.maxParallelDelegations ?? 3,
    retryFailedSubtasks: config?.retryFailedSubtasks ?? true,
    maxRetries: config?.maxRetries ?? 1,
  }

  const results: SubtaskResult[] = []
  const completed = new Set<number>()
  const depsOf = buildDependencyMap(plan)
  const n = plan.subtasks.length
  let batchIndex = 0

  while (completed.size < n) {
    // Find subtask indices that are ready (all deps completed)
    const ready = findReadySubtasks(n, completed, depsOf)

    if (ready.length === 0) {
      log.warn('No ready subtasks but not all completed — possible dependency cycle')
      // Force-add remaining to break the deadlock
      const remaining = Array.from({ length: n }, (_, i) => i).filter((i) => !completed.has(i))
      for (const idx of remaining) completed.add(idx)
      break
    }

    // Execute batch in chunks of maxParallelDelegations
    const batchSubtasks = ready.slice(0, cfg.maxParallelDelegations)

    onEvent?.({
      type: 'orchestration:batch-start',
      batchIndex,
      subtaskIndices: batchSubtasks,
      totalBatches: estimateTotalBatches(n, completed.size, batchSubtasks.length),
    })

    const batchResults = await executeBatch(batchSubtasks, plan, delegateFn, cfg)

    for (const r of batchResults) {
      results.push(r.result)
      completed.add(r.idx)
    }

    const batchSuccess = batchResults.every((r) => r.result.success)
    onEvent?.({
      type: 'orchestration:batch-complete',
      batchIndex,
      success: batchSuccess,
      subtaskIndices: batchSubtasks,
      completedCount: completed.size,
      totalCount: n,
    })

    batchIndex++
  }

  const success = results.every((r) => r.success)
  const summary = buildOrchestrationSummary(plan, results)

  return { plan, results, success, summary }
}

// ─── Helpers ────────────────────────────────────────────────────────────────

/**
 * Build a map from subtask index to its dependency indices.
 * Dependencies are stored as [blocker, blocked] pairs.
 */
function buildDependencyMap(plan: TaskPlan): Map<number, number[]> {
  const deps = new Map<number, number[]>()
  for (const [blocker, blocked] of plan.dependencies) {
    const n = plan.subtasks.length
    if (blocker >= 0 && blocker < n && blocked >= 0 && blocked < n) {
      const existing = deps.get(blocked) ?? []
      existing.push(blocker)
      deps.set(blocked, existing)
    }
  }
  return deps
}

/**
 * Find subtask indices that are ready to execute.
 * A subtask is ready when all its dependencies have completed.
 */
export function findReadySubtasks(
  totalCount: number,
  completed: Set<number>,
  depsOf: Map<number, number[]>
): number[] {
  const ready: number[] = []
  for (let i = 0; i < totalCount; i++) {
    if (completed.has(i)) continue
    const deps = depsOf.get(i)
    if (!deps || deps.every((d) => completed.has(d))) {
      ready.push(i)
    }
  }
  return ready
}

/** Rough estimate of total batches remaining. */
function estimateTotalBatches(
  totalSubtasks: number,
  completedCount: number,
  currentBatchSize: number
): number {
  if (currentBatchSize === 0) return 0
  const remaining = totalSubtasks - completedCount
  return Math.ceil(remaining / currentBatchSize)
}

interface BatchResult {
  idx: number
  result: SubtaskResult
}

async function executeBatch(
  indices: number[],
  plan: TaskPlan,
  delegateFn: (agentId: string, task: string) => Promise<AgentResult>,
  cfg: OrchestratorConfig
): Promise<BatchResult[]> {
  return Promise.all(
    indices.map(async (idx): Promise<BatchResult> => {
      const subtask = plan.subtasks[idx]!
      const start = Date.now()
      const agentId = subtask.assignTo || selectBestAgent(subtask.description)

      let result: AgentResult | undefined
      let attempts = 0
      const maxAttempts = cfg.retryFailedSubtasks ? cfg.maxRetries + 1 : 1

      while (attempts < maxAttempts) {
        attempts++
        try {
          result = await delegateFn(agentId, subtask.description)
          if (result.success) break
        } catch (err) {
          log.warn(`Subtask #${idx} attempt ${attempts} failed: ${err}`)
        }
      }

      return {
        idx,
        result: {
          subtaskId: String(idx),
          agentId,
          success: result?.success ?? false,
          output: result?.output ?? 'Failed after all retries',
          durationMs: Date.now() - start,
        },
      }
    })
  )
}

/**
 * Select the best lead agent for a task based on domain analysis.
 * Uses the router's analyzeDomain to map task description to a domain,
 * then picks the matching lead.
 */
function selectBestAgent(taskDescription: string): string {
  const domain = analyzeDomain(taskDescription)
  const leads = getAgentsByTier('lead')

  const domainMap: Record<string, string> = {
    frontend: 'frontend-lead',
    backend: 'backend-lead',
    testing: 'qa-lead',
    devops: 'fullstack-lead',
    fullstack: 'fullstack-lead',
  }

  const leadId = domainMap[domain] ?? 'fullstack-lead'
  return leads.find((l) => l.id === leadId)?.id ?? 'fullstack-lead'
}

function buildOrchestrationSummary(plan: TaskPlan, results: SubtaskResult[]): string {
  const lines: string[] = [`## Orchestration Results`]
  const succeeded = results.filter((r) => r.success).length
  const failed = results.filter((r) => !r.success).length
  lines.push(`\n${succeeded} succeeded, ${failed} failed out of ${results.length} subtasks\n`)

  for (const r of results) {
    const status = r.success ? '[OK]' : '[FAIL]'
    const idx = parseInt(r.subtaskId, 10)
    const subtask = plan.subtasks[idx]
    const desc = subtask?.description ?? `subtask #${r.subtaskId}`
    lines.push(`${status} ${desc} (${r.agentId}, ${r.durationMs}ms)`)
    if (!r.success) {
      lines.push(`  Error: ${r.output.slice(0, 200)}`)
    }
  }

  return lines.join('\n')
}
