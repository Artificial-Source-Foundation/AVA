/**
 * Delta9 Task Decomposition System - Type Definitions
 *
 * Break complex tasks into subtasks with dependency tracking,
 * validation, and historical search.
 */

import { z } from 'zod'

// =============================================================================
// Strategy Types
// =============================================================================

/** Decomposition strategies */
export const DecompositionStrategySchema = z.enum([
  'file_based', // Group by files to be modified
  'feature_based', // Group by feature/functionality
  'layer_based', // Group by architectural layer (UI, API, DB)
  'test_first', // Start with tests, then implement
  'incremental', // Small incremental changes
])

export type DecompositionStrategy = z.infer<typeof DecompositionStrategySchema>

/** Strategy descriptions for selection help */
export const STRATEGY_DESCRIPTIONS: Record<DecompositionStrategy, string> = {
  file_based:
    'Group subtasks by the files they modify. Best when changes are isolated to specific files.',
  feature_based: 'Group subtasks by feature/functionality. Best for feature implementations.',
  layer_based: 'Group by architectural layer (UI, API, DB). Best for full-stack changes.',
  test_first: 'Write tests first, then implement. Best for TDD workflows.',
  incremental: 'Small, incremental changes. Best for refactoring or risky changes.',
}

// =============================================================================
// Complexity Types
// =============================================================================

/** Subtask complexity levels */
export const SubtaskComplexitySchema = z.enum(['low', 'medium', 'high'])
export type SubtaskComplexity = z.infer<typeof SubtaskComplexitySchema>

/** Overall decomposition complexity */
export const DecompositionComplexitySchema = z.enum(['low', 'medium', 'high', 'critical'])
export type DecompositionComplexity = z.infer<typeof DecompositionComplexitySchema>

// =============================================================================
// Subtask Schema
// =============================================================================

/** A single subtask in a decomposition */
export const SubtaskSchema = z.object({
  /** Unique subtask ID */
  id: z.string(),
  /** Human-readable title */
  title: z.string(),
  /** Detailed description of what needs to be done */
  description: z.string(),
  /** Estimated complexity */
  estimatedComplexity: SubtaskComplexitySchema,
  /** Files this subtask will modify (exclusive ownership) */
  files: z.array(z.string()).optional(),
  /** Files this subtask needs to read (shared access) */
  filesReadonly: z.array(z.string()).optional(),
  /** IDs of subtasks that must complete before this one */
  dependencies: z.array(z.string()).optional(),
  /** Acceptance criteria for this subtask */
  acceptanceCriteria: z.array(z.string()),
  /** Suggested agent to execute this subtask */
  suggestedAgent: z.string().optional(),
  /** Order in execution sequence (1-based) */
  order: z.number().optional(),
  /** Tags for categorization */
  tags: z.array(z.string()).optional(),
})

export type Subtask = z.infer<typeof SubtaskSchema>

// =============================================================================
// Decomposition Schema
// =============================================================================

/** A complete task decomposition */
export const DecompositionSchema = z.object({
  /** Unique decomposition ID */
  id: z.string(),
  /** Parent task ID this decomposition is for */
  parentTaskId: z.string(),
  /** Original task description */
  taskDescription: z.string(),
  /** Strategy used for decomposition */
  strategy: DecompositionStrategySchema,
  /** List of subtasks */
  subtasks: z.array(SubtaskSchema),
  /** Overall estimated complexity */
  totalEstimatedComplexity: DecompositionComplexitySchema,
  /** When decomposition was created */
  createdAt: z.string(),
  /** When decomposition was validated */
  validatedAt: z.string().optional(),
  /** Validation quality score (0-1) */
  validationScore: z.number().min(0).max(1).optional(),
  /** Validation issues found */
  validationIssues: z.array(z.string()).optional(),
  /** Mission ID if part of a mission */
  missionId: z.string().optional(),
  /** Context used for decomposition */
  context: z.record(z.unknown()).optional(),
})

export type Decomposition = z.infer<typeof DecompositionSchema>

// =============================================================================
// Validation Types
// =============================================================================

/** Types of validation issues */
export const ValidationIssueTypeSchema = z.enum([
  'missing_criteria', // Subtask has no acceptance criteria
  'circular_dep', // Circular dependency detected
  'overlapping_files', // Multiple subtasks modify same file
  'too_large', // Subtask is too complex
  'too_vague', // Description is too vague
  'missing_deps', // Dependencies reference non-existent subtasks
  'self_dep', // Subtask depends on itself
  'no_files', // No files specified for file-based decomposition
])

export type ValidationIssueType = z.infer<typeof ValidationIssueTypeSchema>

/** Validation issue severity */
export const ValidationSeveritySchema = z.enum(['warning', 'error'])
export type ValidationSeverity = z.infer<typeof ValidationSeveritySchema>

/** A single validation issue */
export const ValidationIssueSchema = z.object({
  /** Type of issue */
  type: ValidationIssueTypeSchema,
  /** Subtask ID if applicable */
  subtaskId: z.string().optional(),
  /** Human-readable message */
  message: z.string(),
  /** Severity level */
  severity: ValidationSeveritySchema,
})

export type ValidationIssue = z.infer<typeof ValidationIssueSchema>

/** Complete validation quality report */
export const DecompositionQualitySchema = z.object({
  /** Overall quality score (0-1) */
  score: z.number().min(0).max(1),
  /** List of issues found */
  issues: z.array(ValidationIssueSchema),
  /** Suggestions for improvement */
  suggestions: z.array(z.string()),
  /** Whether decomposition passes quality threshold */
  passed: z.boolean(),
})

export type DecompositionQuality = z.infer<typeof DecompositionQualitySchema>

// =============================================================================
// Historical Search Types
// =============================================================================

/** Similar task match from history */
export const SimilarTaskSchema = z.object({
  /** Task ID */
  taskId: z.string(),
  /** Task description */
  description: z.string(),
  /** Similarity score (0-1) */
  similarity: z.number().min(0).max(1),
  /** Strategy that was used */
  strategy: DecompositionStrategySchema,
  /** Whether the task succeeded */
  success: z.boolean(),
  /** Number of subtasks it was decomposed into */
  subtaskCount: z.number(),
  /** Duration if available */
  duration: z.number().optional(),
  /** When the task was executed */
  executedAt: z.string().optional(),
})

export type SimilarTask = z.infer<typeof SimilarTaskSchema>

// =============================================================================
// Engine Configuration
// =============================================================================

/** Decomposition engine configuration */
export interface DecompositionEngineConfig {
  /** Base directory for storage */
  baseDir?: string
  /** Minimum quality score to pass validation (default: 0.7) */
  minQualityScore?: number
  /** Maximum subtasks per decomposition (default: 10) */
  maxSubtasks?: number
  /** Minimum acceptance criteria per subtask (default: 1) */
  minAcceptanceCriteria?: number
  /** Enable historical search (default: true) */
  enableHistoricalSearch?: boolean
  /** Storage path for decomposition history */
  storagePath?: string
}

/** Default configuration */
export const DEFAULT_DECOMPOSITION_CONFIG: Required<DecompositionEngineConfig> = {
  baseDir: process.cwd(),
  minQualityScore: 0.7,
  maxSubtasks: 10,
  minAcceptanceCriteria: 1,
  enableHistoricalSearch: true,
  storagePath: '.delta9/decompositions.jsonl',
}

// =============================================================================
// Storage Types
// =============================================================================

/** Stored decomposition record (for history) */
export const DecompositionRecordSchema = z.object({
  /** Decomposition data */
  decomposition: DecompositionSchema,
  /** Outcome: did it succeed? */
  success: z.boolean().optional(),
  /** Duration of execution */
  duration: z.number().optional(),
  /** When recorded */
  recordedAt: z.string(),
})

export type DecompositionRecord = z.infer<typeof DecompositionRecordSchema>

// =============================================================================
// Result Types
// =============================================================================

/** Result of decompose operation */
export interface DecomposeResult {
  success: boolean
  decomposition?: Decomposition
  quality?: DecompositionQuality
  error?: string
}

/** Result of validation */
export interface ValidateResult {
  success: boolean
  quality?: DecompositionQuality
  error?: string
}

/** Result of similar task search */
export interface SearchResult {
  success: boolean
  similar: SimilarTask[]
  error?: string
}

// =============================================================================
// Event Types
// =============================================================================

/** Decomposition event types */
export type DecompositionEventType = 'created' | 'validated' | 'outcome_recorded'

/** Decomposition event */
export interface DecompositionEvent {
  type: DecompositionEventType
  decompositionId: string
  parentTaskId: string
  timestamp: Date
  strategy?: DecompositionStrategy
  subtaskCount?: number
  quality?: number
  success?: boolean
}

/** Event listener */
export type DecompositionEventListener = (event: DecompositionEvent) => void
