/**
 * Delta9 Event Sourcing System
 *
 * Provides immutable event log, state projections, and replay capabilities.
 *
 * @example
 * ```typescript
 * import { getEventStore, ProjectionBuilder } from './events'
 *
 * // Append events
 * const store = getEventStore()
 * store.append('task.created', { taskId: 't1', title: 'Build feature' })
 * store.append('task.completed', { taskId: 't1', success: true, duration: 5000 })
 *
 * // Query events
 * const taskEvents = store.query({ types: ['task.created', 'task.completed'] })
 *
 * // Build projections
 * const builder = new ProjectionBuilder()
 * const mission = builder.buildMissionProjection()
 * const tasks = builder.buildTaskProjections()
 * ```
 */

// Types
export {
  // Base
  BaseEventSchema,
  type BaseEvent,
  // Mission
  MissionCreatedEventSchema,
  MissionStartedEventSchema,
  MissionCompletedEventSchema,
  MissionFailedEventSchema,
  MissionAbortedEventSchema,
  // Task
  TaskCreatedEventSchema,
  TaskStartedEventSchema,
  TaskProgressEventSchema,
  TaskCompletedEventSchema,
  TaskFailedEventSchema,
  TaskRetriedEventSchema,
  TaskSkippedEventSchema,
  // Council
  CouncilConvenedEventSchema,
  OracleRespondedEventSchema,
  CouncilConsensusEventSchema,
  CouncilTimeoutEventSchema,
  // Agent
  AgentDispatchedEventSchema,
  AgentCompletedEventSchema,
  AgentErrorEventSchema,
  // Validation
  ValidationStartedEventSchema,
  ValidationCheckEventSchema,
  ValidationCompletedEventSchema,
  // Learning
  PatternLearnedEventSchema,
  PatternAppliedEventSchema,
  AntiPatternDetectedEventSchema,
  MemoryStoredEventSchema,
  // File
  FileReservedEventSchema,
  FileReleasedEventSchema,
  FileConflictEventSchema,
  FileChangedEventSchema,
  // System
  SessionStartedEventSchema,
  SessionEndedEventSchema,
  ContextCompactedEventSchema,
  CheckpointCreatedEventSchema,
  CheckpointRestoredEventSchema,
  BudgetWarningEventSchema,
  BudgetExceededEventSchema,
  // Union
  Delta9EventSchema,
  type Delta9Event,
  type EventType,
  EVENT_TYPES,
  EVENT_CATEGORIES,
  type EventCategory,
} from './types.js'

// Store
export {
  EventStore,
  getEventStore,
  resetEventStore,
  type EventStoreOptions,
  type EventQuery,
  type EventStats,
} from './store.js'

// Projections
export {
  ProjectionBuilder,
  getCurrentMissionState,
  getTaskStates,
  getLearningInsights,
  getMetrics,
  type MissionProjection,
  type TaskProjection,
  type CouncilProjection,
  type PatternRecord,
  type LearningProjection,
  type MetricsProjection,
} from './projections.js'
