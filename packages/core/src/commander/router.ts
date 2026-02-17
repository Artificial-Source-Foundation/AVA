/**
 * Task Router
 * Keyword + heuristic based auto-routing to select the best worker
 * for a given task without requiring LLM phone book lookup.
 */

import type { WorkerRegistry } from './registry.js'
import type { WorkerDefinition } from './types.js'

// ============================================================================
// Types
// ============================================================================

export interface TaskAnalysis {
  /** Keywords extracted from the goal */
  keywords: string[]
  /** Whether the goal mentions specific file paths */
  hasCodePaths: boolean
  /** Detected task type */
  taskType: 'write' | 'test' | 'review' | 'research' | 'debug' | 'general'
  /** Confidence in the analysis (0-1) */
  confidence: number
}

// ============================================================================
// Keyword Patterns
// ============================================================================

const TASK_PATTERNS: Array<{
  type: TaskAnalysis['taskType']
  keywords: string[]
  weight: number
}> = [
  {
    type: 'test',
    keywords: ['write test', 'add test', 'test for', 'unit test', 'spec', 'coverage', 'testing'],
    weight: 0.9,
  },
  {
    type: 'review',
    keywords: ['review', 'audit', 'check', 'inspect', 'analyze code', 'code quality'],
    weight: 0.85,
  },
  {
    type: 'research',
    keywords: ['research', 'find', 'search', 'explain', 'understand', 'explore', 'how does'],
    weight: 0.8,
  },
  {
    type: 'debug',
    keywords: ['fix', 'debug', 'error', 'bug', 'broken', 'failing', 'crash', 'issue'],
    weight: 0.85,
  },
  {
    type: 'write',
    keywords: ['implement', 'add', 'create', 'build', 'write', 'refactor', 'update', 'modify'],
    weight: 0.75,
  },
]

const WORKER_TYPE_MAP: Record<string, string> = {
  test: 'tester',
  review: 'reviewer',
  research: 'researcher',
  debug: 'debugger',
  write: 'coder',
}

const CODE_PATH_PATTERN = /(?:\/[\w.-]+){2,}|[\w-]+\.\w{1,5}\b/

// ============================================================================
// Analysis
// ============================================================================

/**
 * Analyze a task goal to determine the best worker type
 */
export function analyzeTask(goal: string, context?: string): TaskAnalysis {
  const text = `${goal} ${context ?? ''}`.toLowerCase()
  const hasCodePaths = CODE_PATH_PATTERN.test(text)

  let bestType: TaskAnalysis['taskType'] = 'general'
  let bestConfidence = 0
  const matchedKeywords: string[] = []

  for (const pattern of TASK_PATTERNS) {
    let matchCount = 0
    for (const keyword of pattern.keywords) {
      if (text.includes(keyword)) {
        matchCount++
        matchedKeywords.push(keyword)
      }
    }
    if (matchCount > 0) {
      const confidence = Math.min(pattern.weight * (1 + (matchCount - 1) * 0.1), 1)
      if (confidence > bestConfidence) {
        bestConfidence = confidence
        bestType = pattern.type
      }
    }
  }

  return {
    keywords: matchedKeywords,
    hasCodePaths,
    taskType: bestType,
    confidence: bestConfidence,
  }
}

/**
 * Select the best worker from the registry based on task analysis
 */
export function selectWorker(
  analysis: TaskAnalysis,
  registry: WorkerRegistry
): WorkerDefinition | null {
  if (analysis.taskType === 'general' || analysis.confidence < 0.5) {
    return null
  }

  const workerName = WORKER_TYPE_MAP[analysis.taskType]
  if (!workerName) return null

  return registry.get(workerName) ?? null
}
