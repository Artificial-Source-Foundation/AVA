/**
 * Delta9 Task Decomposition System
 *
 * Break complex tasks into subtasks with dependency tracking,
 * validation, and historical search.
 *
 * @example
 * ```typescript
 * import { getDecompositionEngine, type Subtask } from './decomposition'
 *
 * const engine = getDecompositionEngine()
 *
 * // Create a decomposition
 * const result = engine.decompose('task-123', 'Implement user authentication', {
 *   strategy: 'feature_based',
 *   subtasks: [
 *     {
 *       id: 'sub-1',
 *       title: 'Create user model',
 *       description: 'Add User schema with email, password fields',
 *       estimatedComplexity: 'low',
 *       files: ['src/models/user.ts'],
 *       acceptanceCriteria: ['User model exports correctly', 'Has password hashing'],
 *     },
 *     // ... more subtasks
 *   ],
 * })
 *
 * // Search for similar tasks
 * const similar = engine.searchSimilarTasks('authentication system', 5)
 *
 * // Record outcome after execution
 * engine.recordOutcome(result.decomposition!.id, true, 15000)
 * ```
 */

// Types
export {
  type Decomposition,
  type Subtask,
  type DecompositionStrategy,
  type DecompositionComplexity,
  type SubtaskComplexity,
  type DecompositionQuality,
  type ValidationIssue,
  type ValidationIssueType,
  type ValidationSeverity,
  type SimilarTask,
  type DecompositionEngineConfig,
  type DecompositionRecord,
  type DecomposeResult,
  type ValidateResult,
  type SearchResult,
  type DecompositionEvent,
  type DecompositionEventType,
  type DecompositionEventListener,
  DecompositionStrategySchema,
  DecompositionComplexitySchema,
  SubtaskComplexitySchema,
  SubtaskSchema,
  DecompositionSchema,
  DecompositionQualitySchema,
  ValidationIssueSchema,
  SimilarTaskSchema,
  DecompositionRecordSchema,
  STRATEGY_DESCRIPTIONS,
  DEFAULT_DECOMPOSITION_CONFIG,
} from './types.js'

// Validator
export { DecompositionValidator } from './validator.js'

// Engine
export { DecompositionEngine, getDecompositionEngine, resetDecompositionEngine } from './engine.js'
