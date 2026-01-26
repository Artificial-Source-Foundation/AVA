/**
 * Delta9 Event Types
 *
 * Comprehensive event definitions for the event sourcing system.
 * All events are immutable and append-only.
 *
 * Event Categories:
 * - Mission: Mission lifecycle events
 * - Task: Task execution events
 * - Council: Oracle consultation events
 * - Agent: Agent dispatch events
 * - Validation: QA validation events
 * - Memory: Learning and memory events
 * - System: System-level events
 */

import { z } from 'zod'

// =============================================================================
// Base Event Schema
// =============================================================================

export const BaseEventSchema = z.object({
  /** Unique event ID */
  id: z.string(),
  /** Event type discriminator */
  type: z.string(),
  /** ISO timestamp */
  timestamp: z.string().datetime(),
  /** Session ID for grouping */
  sessionId: z.string().optional(),
  /** Mission ID if applicable */
  missionId: z.string().optional(),
  /** Correlation ID for tracing */
  correlationId: z.string().optional(),
})

export type BaseEvent = z.infer<typeof BaseEventSchema>

// =============================================================================
// Mission Events
// =============================================================================

export const MissionCreatedEventSchema = BaseEventSchema.extend({
  type: z.literal('mission.created'),
  data: z.object({
    name: z.string(),
    description: z.string().optional(),
    objectives: z.array(z.string()),
    budgetLimit: z.number().optional(),
  }),
})

export const MissionStartedEventSchema = BaseEventSchema.extend({
  type: z.literal('mission.started'),
  data: z.object({
    taskCount: z.number(),
  }),
})

export const MissionCompletedEventSchema = BaseEventSchema.extend({
  type: z.literal('mission.completed'),
  data: z.object({
    success: z.boolean(),
    duration: z.number(),
    tasksCompleted: z.number(),
    tasksFailed: z.number(),
    budgetSpent: z.number().optional(),
  }),
})

export const MissionFailedEventSchema = BaseEventSchema.extend({
  type: z.literal('mission.failed'),
  data: z.object({
    reason: z.string(),
    failedTaskId: z.string().optional(),
    canRetry: z.boolean(),
  }),
})

export const MissionAbortedEventSchema = BaseEventSchema.extend({
  type: z.literal('mission.aborted'),
  data: z.object({
    reason: z.string(),
    abortedBy: z.enum(['user', 'system', 'budget']),
  }),
})

// =============================================================================
// Task Events
// =============================================================================

export const TaskCreatedEventSchema = BaseEventSchema.extend({
  type: z.literal('task.created'),
  data: z.object({
    taskId: z.string(),
    title: z.string(),
    description: z.string().optional(),
    assignedAgent: z.string().optional(),
    priority: z.enum(['low', 'medium', 'high', 'critical']).optional(),
    dependencies: z.array(z.string()).optional(),
  }),
})

export const TaskStartedEventSchema = BaseEventSchema.extend({
  type: z.literal('task.started'),
  data: z.object({
    taskId: z.string(),
    agent: z.string(),
    model: z.string().optional(),
  }),
})

export const TaskProgressEventSchema = BaseEventSchema.extend({
  type: z.literal('task.progress'),
  data: z.object({
    taskId: z.string(),
    progress: z.number().min(0).max(100),
    message: z.string().optional(),
  }),
})

export const TaskCompletedEventSchema = BaseEventSchema.extend({
  type: z.literal('task.completed'),
  data: z.object({
    taskId: z.string(),
    success: z.boolean(),
    duration: z.number(),
    filesChanged: z.array(z.string()).optional(),
    tokensUsed: z.number().optional(),
  }),
})

export const TaskFailedEventSchema = BaseEventSchema.extend({
  type: z.literal('task.failed'),
  data: z.object({
    taskId: z.string(),
    error: z.string(),
    errorCode: z.string().optional(),
    retryable: z.boolean(),
    attempt: z.number(),
  }),
})

export const TaskRetriedEventSchema = BaseEventSchema.extend({
  type: z.literal('task.retried'),
  data: z.object({
    taskId: z.string(),
    attempt: z.number(),
    reason: z.string().optional(),
  }),
})

export const TaskSkippedEventSchema = BaseEventSchema.extend({
  type: z.literal('task.skipped'),
  data: z.object({
    taskId: z.string(),
    reason: z.string(),
  }),
})

// =============================================================================
// Council Events
// =============================================================================

export const CouncilConvenedEventSchema = BaseEventSchema.extend({
  type: z.literal('council.convened'),
  data: z.object({
    mode: z.enum(['none', 'quick', 'standard', 'xhigh']),
    oracles: z.array(z.string()),
    question: z.string(),
  }),
})

export const OracleRespondedEventSchema = BaseEventSchema.extend({
  type: z.literal('council.oracle_responded'),
  data: z.object({
    oracle: z.string(),
    confidence: z.number().min(0).max(1),
    recommendation: z.string(),
    caveats: z.array(z.string()).optional(),
    duration: z.number(),
    tokensUsed: z.number().optional(),
  }),
})

export const CouncilConsensusEventSchema = BaseEventSchema.extend({
  type: z.literal('council.consensus'),
  data: z.object({
    hasConsensus: z.boolean(),
    consensusConfidence: z.number().min(0).max(1),
    recommendation: z.string(),
    dissenting: z.array(z.string()).optional(),
  }),
})

export const CouncilTimeoutEventSchema = BaseEventSchema.extend({
  type: z.literal('council.timeout'),
  data: z.object({
    timedOutOracles: z.array(z.string()),
    partialResult: z.boolean(),
  }),
})

// =============================================================================
// Agent Events
// =============================================================================

export const AgentDispatchedEventSchema = BaseEventSchema.extend({
  type: z.literal('agent.dispatched'),
  data: z.object({
    agent: z.string(),
    taskId: z.string(),
    model: z.string(),
    temperature: z.number().optional(),
    tools: z.array(z.string()).optional(),
  }),
})

export const AgentCompletedEventSchema = BaseEventSchema.extend({
  type: z.literal('agent.completed'),
  data: z.object({
    agent: z.string(),
    taskId: z.string(),
    success: z.boolean(),
    duration: z.number(),
    tokensUsed: z.number().optional(),
  }),
})

export const AgentErrorEventSchema = BaseEventSchema.extend({
  type: z.literal('agent.error'),
  data: z.object({
    agent: z.string(),
    taskId: z.string().optional(),
    error: z.string(),
    errorCode: z.string().optional(),
    recoverable: z.boolean(),
  }),
})

// =============================================================================
// Validation Events
// =============================================================================

export const ValidationStartedEventSchema = BaseEventSchema.extend({
  type: z.literal('validation.started'),
  data: z.object({
    taskId: z.string(),
    validationType: z.enum(['quick', 'full']),
    checks: z.array(z.string()),
  }),
})

export const ValidationCheckEventSchema = BaseEventSchema.extend({
  type: z.literal('validation.check'),
  data: z.object({
    taskId: z.string(),
    check: z.string(),
    passed: z.boolean(),
    message: z.string().optional(),
    duration: z.number().optional(),
  }),
})

export const ValidationCompletedEventSchema = BaseEventSchema.extend({
  type: z.literal('validation.completed'),
  data: z.object({
    taskId: z.string(),
    passed: z.boolean(),
    checksPassed: z.number(),
    checksFailed: z.number(),
    duration: z.number(),
  }),
})

// =============================================================================
// Memory & Learning Events
// =============================================================================

export const PatternLearnedEventSchema = BaseEventSchema.extend({
  type: z.literal('learning.pattern_learned'),
  data: z.object({
    pattern: z.string(),
    context: z.string(),
    confidence: z.number().min(0).max(1),
    source: z.enum(['success', 'failure', 'user', 'inferred']),
  }),
})

export const PatternAppliedEventSchema = BaseEventSchema.extend({
  type: z.literal('learning.pattern_applied'),
  data: z.object({
    pattern: z.string(),
    taskId: z.string(),
    success: z.boolean(),
  }),
})

export const AntiPatternDetectedEventSchema = BaseEventSchema.extend({
  type: z.literal('learning.anti_pattern_detected'),
  data: z.object({
    pattern: z.string(),
    failureRate: z.number(),
    occurrences: z.number(),
  }),
})

export const MemoryStoredEventSchema = BaseEventSchema.extend({
  type: z.literal('memory.stored'),
  data: z.object({
    key: z.string(),
    scope: z.enum(['global', 'project', 'mission']),
    size: z.number(),
  }),
})

// =============================================================================
// File Events
// =============================================================================

export const FileReservedEventSchema = BaseEventSchema.extend({
  type: z.literal('file.reserved'),
  data: z.object({
    path: z.string(),
    agent: z.string(),
    taskId: z.string(),
    exclusive: z.boolean(),
  }),
})

export const FileReleasedEventSchema = BaseEventSchema.extend({
  type: z.literal('file.released'),
  data: z.object({
    path: z.string(),
    agent: z.string(),
  }),
})

export const FileConflictEventSchema = BaseEventSchema.extend({
  type: z.literal('file.conflict'),
  data: z.object({
    path: z.string(),
    requestingAgent: z.string(),
    holdingAgent: z.string(),
  }),
})

export const FileChangedEventSchema = BaseEventSchema.extend({
  type: z.literal('file.changed'),
  data: z.object({
    path: z.string(),
    changeType: z.enum(['created', 'modified', 'deleted']),
    agent: z.string().optional(),
    taskId: z.string().optional(),
  }),
})

// =============================================================================
// Messaging Events
// =============================================================================

export const MessageSentEventSchema = BaseEventSchema.extend({
  type: z.literal('messaging.sent'),
  data: z.object({
    messageId: z.string(),
    from: z.string(),
    to: z.string(),
    type: z.enum(['request', 'response', 'status', 'coordination', 'alert', 'ack']),
    subject: z.string(),
    priority: z.enum(['low', 'normal', 'high', 'critical']),
    taskId: z.string().optional(),
    recipients: z.array(z.string()).optional(),
  }),
})

export const MessageReadEventSchema = BaseEventSchema.extend({
  type: z.literal('messaging.read'),
  data: z.object({
    messageId: z.string(),
    from: z.string(),
    to: z.string(),
    readBy: z.string(),
  }),
})

export const MessageExpiredEventSchema = BaseEventSchema.extend({
  type: z.literal('messaging.expired'),
  data: z.object({
    messageId: z.string(),
    from: z.string(),
    to: z.string(),
    subject: z.string(),
  }),
})

export const MessageBroadcastEventSchema = BaseEventSchema.extend({
  type: z.literal('messaging.broadcast'),
  data: z.object({
    messageId: z.string(),
    from: z.string(),
    group: z.enum(['broadcast', 'council', 'operators', 'support']),
    subject: z.string(),
    recipientCount: z.number(),
  }),
})

// =============================================================================
// Decomposition Events
// =============================================================================

export const DecompositionCreatedEventSchema = BaseEventSchema.extend({
  type: z.literal('decomposition.created'),
  data: z.object({
    decompositionId: z.string(),
    parentTaskId: z.string(),
    strategy: z.enum(['file_based', 'feature_based', 'layer_based', 'test_first', 'incremental']),
    subtaskCount: z.number(),
    totalComplexity: z.enum(['low', 'medium', 'high', 'critical']),
    qualityScore: z.number().min(0).max(1).optional(),
  }),
})

export const DecompositionValidatedEventSchema = BaseEventSchema.extend({
  type: z.literal('decomposition.validated'),
  data: z.object({
    decompositionId: z.string(),
    parentTaskId: z.string(),
    qualityScore: z.number().min(0).max(1),
    passed: z.boolean(),
    issueCount: z.number(),
    suggestionCount: z.number(),
  }),
})

export const DecompositionOutcomeEventSchema = BaseEventSchema.extend({
  type: z.literal('decomposition.outcome_recorded'),
  data: z.object({
    decompositionId: z.string(),
    parentTaskId: z.string(),
    strategy: z.enum(['file_based', 'feature_based', 'layer_based', 'test_first', 'incremental']),
    success: z.boolean(),
    duration: z.number().optional(),
  }),
})

// =============================================================================
// Epic Events
// =============================================================================

export const EpicCreatedEventSchema = BaseEventSchema.extend({
  type: z.literal('epic.created'),
  data: z.object({
    epicId: z.string(),
    title: z.string(),
    priority: z.enum(['low', 'normal', 'high', 'critical']),
  }),
})

export const EpicTaskLinkedEventSchema = BaseEventSchema.extend({
  type: z.literal('epic.task_linked'),
  data: z.object({
    epicId: z.string(),
    taskId: z.string(),
    objectiveId: z.string().optional(),
  }),
})

export const EpicStatusChangedEventSchema = BaseEventSchema.extend({
  type: z.literal('epic.status_changed'),
  data: z.object({
    epicId: z.string(),
    previousStatus: z.enum(['planning', 'in_progress', 'completed', 'blocked', 'archived']),
    newStatus: z.enum(['planning', 'in_progress', 'completed', 'blocked', 'archived']),
  }),
})

export const EpicCompletedEventSchema = BaseEventSchema.extend({
  type: z.literal('epic.completed'),
  data: z.object({
    epicId: z.string(),
    title: z.string(),
    taskCount: z.number(),
    duration: z.number().optional(),
  }),
})

// =============================================================================
// Decision Trace Events
// =============================================================================

export const DecisionTracedEventSchema = BaseEventSchema.extend({
  type: z.literal('decision.traced'),
  data: z.object({
    traceId: z.string(),
    decisionType: z.enum([
      'decomposition_strategy',
      'agent_assignment',
      'council_consensus',
      'validation_override',
      'conflict_resolution',
      'model_selection',
      'retry_strategy',
      'priority_change',
      'task_skip',
      'budget_decision',
    ]),
    decision: z.string(),
    confidence: z.number().min(0).max(1),
    hasPrecedents: z.boolean(),
  }),
})

// =============================================================================
// System Events
// =============================================================================

export const SessionStartedEventSchema = BaseEventSchema.extend({
  type: z.literal('system.session_started'),
  data: z.object({
    version: z.string(),
    config: z.record(z.unknown()).optional(),
  }),
})

export const SessionEndedEventSchema = BaseEventSchema.extend({
  type: z.literal('system.session_ended'),
  data: z.object({
    reason: z.enum(['completed', 'aborted', 'error', 'timeout']),
    duration: z.number(),
  }),
})

export const ContextCompactedEventSchema = BaseEventSchema.extend({
  type: z.literal('system.context_compacted'),
  data: z.object({
    tokensBefore: z.number(),
    tokensAfter: z.number(),
    preservedState: z.boolean(),
  }),
})

export const CheckpointCreatedEventSchema = BaseEventSchema.extend({
  type: z.literal('system.checkpoint_created'),
  data: z.object({
    checkpointId: z.string(),
    name: z.string().optional(),
    taskProgress: z.number(),
  }),
})

export const CheckpointRestoredEventSchema = BaseEventSchema.extend({
  type: z.literal('system.checkpoint_restored'),
  data: z.object({
    checkpointId: z.string(),
    eventsReplayed: z.number(),
  }),
})

export const BudgetWarningEventSchema = BaseEventSchema.extend({
  type: z.literal('system.budget_warning'),
  data: z.object({
    spent: z.number(),
    limit: z.number(),
    percentage: z.number(),
  }),
})

export const BudgetExceededEventSchema = BaseEventSchema.extend({
  type: z.literal('system.budget_exceeded'),
  data: z.object({
    spent: z.number(),
    limit: z.number(),
  }),
})

export const NotificationEventSchema = BaseEventSchema.extend({
  type: z.literal('system.notification'),
  data: z.object({
    notificationId: z.string(),
    type: z.enum(['info', 'success', 'warning', 'error', 'progress']),
    title: z.string(),
    message: z.string().optional(),
    taskId: z.string().optional(),
    agent: z.string().optional(),
  }),
})

// =============================================================================
// Union Types
// =============================================================================

export const Delta9EventSchema = z.discriminatedUnion('type', [
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
  // Messaging
  MessageSentEventSchema,
  MessageReadEventSchema,
  MessageExpiredEventSchema,
  MessageBroadcastEventSchema,
  // Decomposition
  DecompositionCreatedEventSchema,
  DecompositionValidatedEventSchema,
  DecompositionOutcomeEventSchema,
  // Epic
  EpicCreatedEventSchema,
  EpicTaskLinkedEventSchema,
  EpicStatusChangedEventSchema,
  EpicCompletedEventSchema,
  // Decision Traces
  DecisionTracedEventSchema,
  // System
  SessionStartedEventSchema,
  SessionEndedEventSchema,
  ContextCompactedEventSchema,
  CheckpointCreatedEventSchema,
  CheckpointRestoredEventSchema,
  BudgetWarningEventSchema,
  BudgetExceededEventSchema,
  NotificationEventSchema,
])

export type Delta9Event = z.infer<typeof Delta9EventSchema>

// =============================================================================
// Event Type Helpers
// =============================================================================

export type EventType = Delta9Event['type']

export const EVENT_TYPES = {
  // Mission
  MISSION_CREATED: 'mission.created',
  MISSION_STARTED: 'mission.started',
  MISSION_COMPLETED: 'mission.completed',
  MISSION_FAILED: 'mission.failed',
  MISSION_ABORTED: 'mission.aborted',
  // Task
  TASK_CREATED: 'task.created',
  TASK_STARTED: 'task.started',
  TASK_PROGRESS: 'task.progress',
  TASK_COMPLETED: 'task.completed',
  TASK_FAILED: 'task.failed',
  TASK_RETRIED: 'task.retried',
  TASK_SKIPPED: 'task.skipped',
  // Council
  COUNCIL_CONVENED: 'council.convened',
  ORACLE_RESPONDED: 'council.oracle_responded',
  COUNCIL_CONSENSUS: 'council.consensus',
  COUNCIL_TIMEOUT: 'council.timeout',
  // Agent
  AGENT_DISPATCHED: 'agent.dispatched',
  AGENT_COMPLETED: 'agent.completed',
  AGENT_ERROR: 'agent.error',
  // Validation
  VALIDATION_STARTED: 'validation.started',
  VALIDATION_CHECK: 'validation.check',
  VALIDATION_COMPLETED: 'validation.completed',
  // Learning
  PATTERN_LEARNED: 'learning.pattern_learned',
  PATTERN_APPLIED: 'learning.pattern_applied',
  ANTI_PATTERN_DETECTED: 'learning.anti_pattern_detected',
  MEMORY_STORED: 'memory.stored',
  // File
  FILE_RESERVED: 'file.reserved',
  FILE_RELEASED: 'file.released',
  FILE_CONFLICT: 'file.conflict',
  FILE_CHANGED: 'file.changed',
  // Messaging
  MESSAGE_SENT: 'messaging.sent',
  MESSAGE_READ: 'messaging.read',
  MESSAGE_EXPIRED: 'messaging.expired',
  MESSAGE_BROADCAST: 'messaging.broadcast',
  // Decomposition
  DECOMPOSITION_CREATED: 'decomposition.created',
  DECOMPOSITION_VALIDATED: 'decomposition.validated',
  DECOMPOSITION_OUTCOME_RECORDED: 'decomposition.outcome_recorded',
  // Epic
  EPIC_CREATED: 'epic.created',
  EPIC_TASK_LINKED: 'epic.task_linked',
  EPIC_STATUS_CHANGED: 'epic.status_changed',
  EPIC_COMPLETED: 'epic.completed',
  // System
  SESSION_STARTED: 'system.session_started',
  SESSION_ENDED: 'system.session_ended',
  CONTEXT_COMPACTED: 'system.context_compacted',
  CHECKPOINT_CREATED: 'system.checkpoint_created',
  CHECKPOINT_RESTORED: 'system.checkpoint_restored',
  BUDGET_WARNING: 'system.budget_warning',
  BUDGET_EXCEEDED: 'system.budget_exceeded',
  NOTIFICATION: 'system.notification',
} as const

// =============================================================================
// Event Categories
// =============================================================================

export const EVENT_CATEGORIES = {
  mission: [
    'mission.created',
    'mission.started',
    'mission.completed',
    'mission.failed',
    'mission.aborted',
  ],
  task: [
    'task.created',
    'task.started',
    'task.progress',
    'task.completed',
    'task.failed',
    'task.retried',
    'task.skipped',
  ],
  council: ['council.convened', 'council.oracle_responded', 'council.consensus', 'council.timeout'],
  agent: ['agent.dispatched', 'agent.completed', 'agent.error'],
  validation: ['validation.started', 'validation.check', 'validation.completed'],
  learning: [
    'learning.pattern_learned',
    'learning.pattern_applied',
    'learning.anti_pattern_detected',
    'memory.stored',
  ],
  file: ['file.reserved', 'file.released', 'file.conflict', 'file.changed'],
  messaging: ['messaging.sent', 'messaging.read', 'messaging.expired', 'messaging.broadcast'],
  decomposition: [
    'decomposition.created',
    'decomposition.validated',
    'decomposition.outcome_recorded',
  ],
  epic: ['epic.created', 'epic.task_linked', 'epic.status_changed', 'epic.completed'],
  decision: ['decision.traced'],
  system: [
    'system.session_started',
    'system.session_ended',
    'system.context_compacted',
    'system.checkpoint_created',
    'system.checkpoint_restored',
    'system.budget_warning',
    'system.budget_exceeded',
    'system.notification',
  ],
} as const

export type EventCategory = keyof typeof EVENT_CATEGORIES
