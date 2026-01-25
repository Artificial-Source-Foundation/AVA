/**
 * Auto-Scaling System Types
 *
 * Dynamic resource management for Delta9.
 */

import { z } from 'zod'

// =============================================================================
// Scaling Targets
// =============================================================================

export type ScalingTarget = 'operators' | 'oracles' | 'validators' | 'webhooks'

export const scalingConfigSchema = z.object({
  /** Target to scale */
  target: z.enum(['operators', 'oracles', 'validators', 'webhooks']),
  /** Minimum instances */
  minInstances: z.number().int().min(1).default(1),
  /** Maximum instances */
  maxInstances: z.number().int().min(1).default(10),
  /** Target utilization (0-1) */
  targetUtilization: z.number().min(0).max(1).default(0.7),
  /** Scale up threshold */
  scaleUpThreshold: z.number().min(0).max(1).default(0.8),
  /** Scale down threshold */
  scaleDownThreshold: z.number().min(0).max(1).default(0.3),
  /** Cooldown period in seconds */
  cooldownSeconds: z.number().int().min(0).default(60),
  /** Evaluation window in seconds */
  evaluationWindowSeconds: z.number().int().min(10).default(30),
  /** Scale up step */
  scaleUpStep: z.number().int().min(1).default(1),
  /** Scale down step */
  scaleDownStep: z.number().int().min(1).default(1),
  /** Enabled */
  enabled: z.boolean().default(true),
})

export type ScalingConfig = z.infer<typeof scalingConfigSchema>

// =============================================================================
// Scaling Metrics
// =============================================================================

export interface ScalingMetrics {
  /** Current instance count */
  currentInstances: number
  /** Target utilization */
  targetUtilization: number
  /** Current utilization */
  currentUtilization: number
  /** Queue depth */
  queueDepth: number
  /** Average processing time */
  avgProcessingTime: number
  /** Throughput (items/second) */
  throughput: number
  /** Error rate */
  errorRate: number
  /** Timestamp */
  timestamp: string
}

// =============================================================================
// Scaling Decisions
// =============================================================================

export type ScalingAction = 'scale_up' | 'scale_down' | 'no_action'

export interface ScalingDecision {
  /** Target */
  target: ScalingTarget
  /** Action */
  action: ScalingAction
  /** Current count */
  currentCount: number
  /** Desired count */
  desiredCount: number
  /** Reason */
  reason: string
  /** Metrics that triggered decision */
  metrics: ScalingMetrics
  /** Timestamp */
  timestamp: string
}

// =============================================================================
// Scaling Policy
// =============================================================================

export type ScalingPolicyType = 'target_tracking' | 'step_scaling' | 'scheduled' | 'predictive'

export interface ScalingPolicy {
  /** Policy ID */
  id: string
  /** Policy name */
  name: string
  /** Policy type */
  type: ScalingPolicyType
  /** Target */
  target: ScalingTarget
  /** Configuration */
  config: ScalingConfig
  /** Schedule for scheduled policies */
  schedule?: ScheduledScaling[]
  /** Prediction config for predictive policies */
  prediction?: PredictiveConfig
  /** Step config for step scaling */
  steps?: ScalingStep[]
  /** Priority (lower = higher priority) */
  priority: number
  /** Enabled */
  enabled: boolean
}

export interface ScheduledScaling {
  /** Cron expression or time */
  cron?: string
  /** Specific time (ISO format) */
  at?: string
  /** Desired instance count */
  desiredCount: number
  /** Duration in seconds (optional) */
  duration?: number
}

export interface PredictiveConfig {
  /** Lookback window in hours */
  lookbackHours: number
  /** Forecast horizon in minutes */
  forecastMinutes: number
  /** Seasonality (daily, weekly, monthly) */
  seasonality?: 'daily' | 'weekly' | 'monthly'
  /** Minimum confidence for prediction */
  minConfidence: number
}

export interface ScalingStep {
  /** Metric threshold */
  threshold: number
  /** Comparison operator */
  operator: 'gt' | 'lt' | 'gte' | 'lte'
  /** Adjustment type */
  adjustmentType: 'exact' | 'change' | 'percent'
  /** Adjustment value */
  adjustment: number
}

// =============================================================================
// Scaling State
// =============================================================================

export interface ScalingState {
  /** Target */
  target: ScalingTarget
  /** Current instance count */
  currentInstances: number
  /** Desired instance count */
  desiredInstances: number
  /** Last scaling action */
  lastAction?: ScalingDecision
  /** Last scaling time */
  lastScalingTime?: string
  /** In cooldown */
  inCooldown: boolean
  /** Cooldown ends at */
  cooldownEndsAt?: string
  /** History of recent decisions */
  history: ScalingDecision[]
}

// =============================================================================
// Resource Metrics
// =============================================================================

export interface ResourceMetrics {
  /** CPU utilization (0-1) */
  cpu: number
  /** Memory utilization (0-1) */
  memory: number
  /** Active connections */
  connections: number
  /** Queue size */
  queueSize: number
  /** Processing rate */
  processingRate: number
  /** Error count */
  errors: number
  /** Latency percentiles */
  latency: {
    p50: number
    p90: number
    p99: number
  }
}

// =============================================================================
// Scaling Events
// =============================================================================

export type ScalingEventType =
  | 'scaling.evaluation'
  | 'scaling.decision'
  | 'scaling.started'
  | 'scaling.completed'
  | 'scaling.failed'
  | 'scaling.cooldown'
  | 'scaling.policy.added'
  | 'scaling.policy.removed'

export interface ScalingEvent {
  type: ScalingEventType
  timestamp: string
  target: ScalingTarget
  data: Record<string, unknown>
}

// =============================================================================
// Instance Types
// =============================================================================

export interface ScalableInstance {
  /** Instance ID */
  id: string
  /** Instance type */
  type: ScalingTarget
  /** Status */
  status: 'starting' | 'running' | 'stopping' | 'stopped'
  /** Started at */
  startedAt: string
  /** Current load */
  load: number
  /** Tasks processed */
  tasksProcessed: number
  /** Errors */
  errors: number
  /** Metadata */
  metadata: Record<string, unknown>
}

// =============================================================================
// Capacity Planning
// =============================================================================

export interface CapacityPlan {
  /** Target */
  target: ScalingTarget
  /** Current capacity */
  currentCapacity: number
  /** Recommended capacity */
  recommendedCapacity: number
  /** Peak expected load */
  peakLoad: number
  /** Recommendation confidence */
  confidence: number
  /** Time horizon */
  timeHorizon: string
  /** Reasoning */
  reasoning: string
  /** Cost impact */
  costImpact: {
    current: number
    recommended: number
    savings: number
  }
}

// =============================================================================
// Limits
// =============================================================================

export interface ScalingLimits {
  /** Maximum total instances across all targets */
  maxTotalInstances: number
  /** Maximum cost per hour */
  maxCostPerHour: number
  /** Maximum scaling operations per hour */
  maxScalingOperationsPerHour: number
  /** Rate limit scaling during incidents */
  incidentRateLimit: boolean
}
