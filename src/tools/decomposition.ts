/**
 * Delta9 Decomposition Tools
 *
 * Tools for task decomposition:
 * - decompose_task: Break a task into subtasks
 * - validate_decomposition: Check decomposition quality
 * - search_similar_tasks: Find similar tasks from history
 * - redecompose: Re-decompose with a different strategy
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import {
  getDecompositionEngine,
  type DecompositionStrategy,
  type SubtaskComplexity,
  type Subtask,
} from '../decomposition/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Factory
// =============================================================================

export interface DecompositionToolsConfig {
  /** Base directory for storage */
  baseDir?: string
  /** Logger function */
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

/**
 * Create decomposition tools with bound context
 */
export function createDecompositionTools(
  config: DecompositionToolsConfig = {}
): Record<string, ToolDefinition> {
  const { baseDir, log } = config
  const engine = getDecompositionEngine({ baseDir })

  /**
   * Decompose a task into subtasks
   */
  const decompose_task = tool({
    description: `Break a complex task into smaller, manageable subtasks.

Available strategies:
- file_based: Group by files to be modified (best when changes are isolated)
- feature_based: Group by feature/functionality (best for feature implementations)
- layer_based: Group by layer - UI, API, DB (best for full-stack changes)
- test_first: Write tests first, then implement (best for TDD)
- incremental: Small incremental changes (best for refactoring)

If no strategy is specified, one will be automatically selected based on the task description.`,
    args: {
      taskId: s.string().describe('Unique ID for the parent task'),
      description: s.string().describe('Full description of the task to decompose'),
      strategy: s
        .string()
        .optional()
        .describe(
          'Decomposition strategy (file_based, feature_based, layer_based, test_first, incremental)'
        ),
      subtasks: s
        .string()
        .describe(
          'JSON array of subtasks. Each subtask needs: title, description, estimatedComplexity (low/medium/high), acceptanceCriteria (array), optionally: files, filesReadonly, dependencies, suggestedAgent'
        ),
      missionId: s.string().optional().describe('Related mission ID'),
      useHistory: s
        .boolean()
        .optional()
        .describe('Search for similar tasks in history (default: true)'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Decomposing task', { taskId: args.taskId, strategy: args.strategy })

      // Parse subtasks JSON
      let subtasks: Subtask[]
      try {
        const parsed = JSON.parse(args.subtasks)
        if (!Array.isArray(parsed)) {
          return JSON.stringify({
            success: false,
            error: 'subtasks must be a JSON array',
          })
        }
        subtasks = parsed.map((s: Partial<Subtask>) => ({
          id: s.id || '',
          title: s.title || 'Untitled',
          description: s.description || '',
          estimatedComplexity: (s.estimatedComplexity as SubtaskComplexity) || 'medium',
          acceptanceCriteria: s.acceptanceCriteria || [],
          files: s.files,
          filesReadonly: s.filesReadonly,
          dependencies: s.dependencies,
          suggestedAgent: s.suggestedAgent,
          tags: s.tags,
        }))
      } catch (e) {
        return JSON.stringify({
          success: false,
          error: `Failed to parse subtasks JSON: ${e instanceof Error ? e.message : 'Unknown error'}`,
        })
      }

      const result = engine.decompose(args.taskId, args.description, {
        strategy: args.strategy as DecompositionStrategy | undefined,
        subtasks,
        missionId: args.missionId,
        useHistory: args.useHistory,
      })

      if (result.success && result.decomposition) {
        return JSON.stringify({
          success: true,
          decomposition: {
            id: result.decomposition.id,
            parentTaskId: result.decomposition.parentTaskId,
            strategy: result.decomposition.strategy,
            totalEstimatedComplexity: result.decomposition.totalEstimatedComplexity,
            subtaskCount: result.decomposition.subtasks.length,
            subtasks: result.decomposition.subtasks.map((s) => ({
              id: s.id,
              title: s.title,
              order: s.order,
              estimatedComplexity: s.estimatedComplexity,
              dependencies: s.dependencies,
              files: s.files,
              suggestedAgent: s.suggestedAgent,
            })),
          },
          quality: result.quality
            ? {
                score: result.quality.score,
                passed: result.quality.passed,
                issueCount: result.quality.issues.length,
                suggestionCount: result.quality.suggestions.length,
              }
            : undefined,
          similarTasks: result.decomposition.context?.similarTasks,
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }
    },
  })

  /**
   * Validate a decomposition's quality
   */
  const validate_decomposition = tool({
    description: `Validate the quality of a task decomposition.

Checks for:
- Circular dependencies between subtasks
- File overlaps (multiple subtasks modifying same file)
- Missing or vague acceptance criteria
- Subtasks that are too large or complex
- Invalid dependency references

Returns a quality score (0-1) and list of issues/suggestions.`,
    args: {
      decompositionJson: s
        .string()
        .describe('JSON representation of the decomposition to validate'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Validating decomposition')

      // Parse decomposition
      let decomposition
      try {
        decomposition = JSON.parse(args.decompositionJson)
      } catch (e) {
        return JSON.stringify({
          success: false,
          error: `Failed to parse decomposition JSON: ${e instanceof Error ? e.message : 'Unknown error'}`,
        })
      }

      const result = engine.validate(decomposition)

      if (result.success && result.quality) {
        return JSON.stringify({
          success: true,
          quality: {
            score: result.quality.score,
            passed: result.quality.passed,
            issues: result.quality.issues.map((i) => ({
              type: i.type,
              severity: i.severity,
              message: i.message,
              subtaskId: i.subtaskId,
            })),
            suggestions: result.quality.suggestions,
          },
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error || 'Validation failed',
        })
      }
    },
  })

  /**
   * Search for similar tasks in history
   */
  const search_similar_tasks = tool({
    description: `Search for similar tasks from decomposition history.

Uses token-based similarity to find tasks with similar descriptions.
Returns tasks with their strategies, success rates, and subtask counts.

Useful for:
- Learning from past approaches
- Finding proven strategies for similar problems
- Avoiding repeating failures`,
    args: {
      description: s.string().describe('Description of the task to find similar matches for'),
      limit: s
        .number()
        .optional()
        .describe('Maximum number of similar tasks to return (default: 5)'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Searching similar tasks', { limit: args.limit })

      const result = engine.searchSimilarTasks(args.description, args.limit || 5)

      if (result.success) {
        return JSON.stringify({
          success: true,
          found: result.similar.length,
          similar: result.similar.map((t) => ({
            taskId: t.taskId,
            description:
              t.description.length > 100 ? t.description.substring(0, 100) + '...' : t.description,
            similarity: Math.round(t.similarity * 100) + '%',
            strategy: t.strategy,
            success: t.success,
            subtaskCount: t.subtaskCount,
            duration: t.duration ? `${Math.round(t.duration / 1000)}s` : undefined,
          })),
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }
    },
  })

  /**
   * Re-decompose with a different strategy
   */
  const redecompose = tool({
    description: `Re-decompose a task using a different strategy.

Retrieves the original decomposition and creates a new one with:
- The specified strategy
- Optionally updated subtasks
- Reference to the previous decomposition

Use when the original decomposition didn't work well.`,
    args: {
      decompositionId: s.string().describe('ID of the decomposition to re-decompose'),
      newStrategy: s
        .string()
        .describe(
          'New strategy to use (file_based, feature_based, layer_based, test_first, incremental)'
        ),
      newSubtasks: s
        .string()
        .optional()
        .describe('Optional: New subtasks JSON array (if not provided, uses original subtasks)'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Re-decomposing task', {
        decompositionId: args.decompositionId,
        newStrategy: args.newStrategy,
      })

      // Parse new subtasks if provided
      let newSubtasks: Subtask[] | undefined
      if (args.newSubtasks) {
        try {
          const parsed = JSON.parse(args.newSubtasks)
          if (!Array.isArray(parsed)) {
            return JSON.stringify({
              success: false,
              error: 'newSubtasks must be a JSON array',
            })
          }
          newSubtasks = parsed
        } catch (e) {
          return JSON.stringify({
            success: false,
            error: `Failed to parse newSubtasks JSON: ${e instanceof Error ? e.message : 'Unknown error'}`,
          })
        }
      }

      const result = engine.redecompose(
        args.decompositionId,
        args.newStrategy as DecompositionStrategy,
        newSubtasks
      )

      if (result.success && result.decomposition) {
        return JSON.stringify({
          success: true,
          decomposition: {
            id: result.decomposition.id,
            parentTaskId: result.decomposition.parentTaskId,
            strategy: result.decomposition.strategy,
            totalEstimatedComplexity: result.decomposition.totalEstimatedComplexity,
            subtaskCount: result.decomposition.subtasks.length,
            previousDecompositionId: result.decomposition.context?.previousDecompositionId,
          },
          quality: result.quality
            ? {
                score: result.quality.score,
                passed: result.quality.passed,
              }
            : undefined,
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }
    },
  })

  /**
   * List available decomposition strategies
   */
  const list_strategies = tool({
    description: 'List all available decomposition strategies with descriptions.',
    args: {},

    async execute(_args, _ctx) {
      log?.('debug', 'Listing strategies')

      const strategies = engine.getStrategies()
      const stats = engine.getStats()

      return JSON.stringify({
        success: true,
        strategies: strategies.map((s) => ({
          name: s.strategy,
          description: s.description,
          usageCount: stats.byStrategy[s.strategy] || 0,
        })),
        overallStats: {
          totalDecompositions: stats.totalDecompositions,
          successRate: Math.round(stats.successRate * 100) + '%',
          averageSubtaskCount: Math.round(stats.averageSubtaskCount * 10) / 10,
        },
      })
    },
  })

  /**
   * Record the outcome of a decomposition
   */
  const record_decomposition_outcome = tool({
    description: `Record whether a decomposition succeeded or failed.

This helps the learning system improve future decomposition suggestions.
Call this after all subtasks in a decomposition have been executed.`,
    args: {
      decompositionId: s.string().describe('ID of the decomposition'),
      success: s.boolean().describe('Whether the decomposition was successful'),
      duration: s.number().optional().describe('Total execution duration in milliseconds'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Recording decomposition outcome', {
        decompositionId: args.decompositionId,
        success: args.success,
      })

      const recorded = engine.recordOutcome(args.decompositionId, args.success, args.duration)

      if (recorded) {
        return JSON.stringify({
          success: true,
          message: `Recorded ${args.success ? 'successful' : 'failed'} outcome for decomposition ${args.decompositionId}`,
        })
      } else {
        return JSON.stringify({
          success: false,
          error: `Decomposition ${args.decompositionId} not found`,
        })
      }
    },
  })

  return {
    decompose_task,
    validate_decomposition,
    search_similar_tasks,
    redecompose,
    list_strategies,
    record_decomposition_outcome,
  }
}
