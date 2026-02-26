/**
 * Task router — analyzes goals and routes to the best worker.
 */

import type { TaskAnalysis, TaskType, WorkerDefinition } from './types.js'

interface RoutePattern {
  type: TaskType
  keywords: string[]
  weight: number
}

const ROUTE_PATTERNS: RoutePattern[] = [
  {
    type: 'test',
    keywords: ['write test', 'add test', 'unit test', 'spec', 'coverage', 'test file'],
    weight: 0.9,
  },
  {
    type: 'review',
    keywords: ['review', 'audit', 'check', 'inspect', 'analyze code'],
    weight: 0.85,
  },
  {
    type: 'research',
    keywords: ['research', 'find', 'search', 'explain', 'understand', 'explore'],
    weight: 0.8,
  },
  {
    type: 'debug',
    keywords: ['fix', 'debug', 'error', 'bug', 'broken', 'failing', 'crash'],
    weight: 0.85,
  },
  {
    type: 'write',
    keywords: ['implement', 'add', 'create', 'build', 'write', 'refactor', 'update'],
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

export function analyzeTask(goal: string): TaskAnalysis {
  const lower = goal.toLowerCase()
  const matchedKeywords: string[] = []
  let bestType: TaskType = 'general'
  let bestConfidence = 0

  for (const pattern of ROUTE_PATTERNS) {
    let matchCount = 0
    for (const kw of pattern.keywords) {
      if (lower.includes(kw)) {
        matchCount++
        matchedKeywords.push(kw)
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
    taskType: bestType,
    confidence: bestConfidence,
  }
}

export function selectWorker(
  analysis: TaskAnalysis,
  workers: WorkerDefinition[],
  minConfidence = 0.7
): WorkerDefinition | null {
  if (analysis.confidence < minConfidence) return null

  const targetName = WORKER_TYPE_MAP[analysis.taskType]
  if (!targetName) return null

  return workers.find((w) => w.name === targetName) ?? null
}

export function getFilteredTools(allowedToolNames: string[]): string[] {
  return allowedToolNames.filter((t) => !t.startsWith('delegate_'))
}
