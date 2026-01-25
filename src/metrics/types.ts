/**
 * Metrics Dashboard Types
 *
 * Real-time monitoring and analytics for Delta9.
 */

import { z } from 'zod'

// =============================================================================
// Metric Types
// =============================================================================

export type MetricType = 'counter' | 'gauge' | 'histogram' | 'timer'

export const metricDefinitionSchema = z.object({
  /** Metric name */
  name: z.string(),
  /** Metric type */
  type: z.enum(['counter', 'gauge', 'histogram', 'timer']),
  /** Description */
  description: z.string().optional(),
  /** Unit (e.g., 'ms', 'bytes', 'tokens') */
  unit: z.string().optional(),
  /** Labels/dimensions */
  labels: z.array(z.string()).default([]),
})

export type MetricDefinition = z.infer<typeof metricDefinitionSchema>

// =============================================================================
// Metric Values
// =============================================================================

export interface MetricValue {
  /** Metric name */
  name: string
  /** Value */
  value: number
  /** Timestamp */
  timestamp: string
  /** Labels */
  labels: Record<string, string>
}

export interface CounterValue extends MetricValue {
  type: 'counter'
}

export interface GaugeValue extends MetricValue {
  type: 'gauge'
}

export interface HistogramValue extends MetricValue {
  type: 'histogram'
  /** Bucket counts */
  buckets: Array<{ le: number; count: number }>
  /** Sum of all values */
  sum: number
  /** Count of values */
  count: number
}

export interface TimerValue extends MetricValue {
  type: 'timer'
  /** Minimum value */
  min: number
  /** Maximum value */
  max: number
  /** Average value */
  avg: number
  /** Percentiles */
  percentiles: Record<string, number>
}

// =============================================================================
// Predefined Metrics
// =============================================================================

export const DELTA9_METRICS = {
  // Mission metrics
  missions_total: {
    name: 'delta9_missions_total',
    type: 'counter' as const,
    description: 'Total missions created',
    labels: ['status'],
  },
  missions_active: {
    name: 'delta9_missions_active',
    type: 'gauge' as const,
    description: 'Currently active missions',
  },
  mission_duration_seconds: {
    name: 'delta9_mission_duration_seconds',
    type: 'histogram' as const,
    description: 'Mission duration in seconds',
    unit: 'seconds',
    labels: ['status'],
  },

  // Task metrics
  tasks_total: {
    name: 'delta9_tasks_total',
    type: 'counter' as const,
    description: 'Total tasks executed',
    labels: ['status', 'type'],
  },
  tasks_active: {
    name: 'delta9_tasks_active',
    type: 'gauge' as const,
    description: 'Currently executing tasks',
  },
  task_duration_seconds: {
    name: 'delta9_task_duration_seconds',
    type: 'histogram' as const,
    description: 'Task duration in seconds',
    unit: 'seconds',
    labels: ['type', 'agent'],
  },
  task_retries_total: {
    name: 'delta9_task_retries_total',
    type: 'counter' as const,
    description: 'Total task retries',
    labels: ['reason'],
  },

  // Council metrics
  council_convocations_total: {
    name: 'delta9_council_convocations_total',
    type: 'counter' as const,
    description: 'Total council convocations',
    labels: ['mode'],
  },
  council_duration_seconds: {
    name: 'delta9_council_duration_seconds',
    type: 'histogram' as const,
    description: 'Council deliberation duration',
    unit: 'seconds',
    labels: ['mode'],
  },
  council_consensus_score: {
    name: 'delta9_council_consensus_score',
    type: 'gauge' as const,
    description: 'Latest council consensus score',
    labels: ['mode'],
  },

  // Oracle metrics
  oracle_responses_total: {
    name: 'delta9_oracle_responses_total',
    type: 'counter' as const,
    description: 'Total oracle responses',
    labels: ['oracle', 'status'],
  },
  oracle_latency_seconds: {
    name: 'delta9_oracle_latency_seconds',
    type: 'histogram' as const,
    description: 'Oracle response latency',
    unit: 'seconds',
    labels: ['oracle'],
  },
  oracle_confidence: {
    name: 'delta9_oracle_confidence',
    type: 'gauge' as const,
    description: 'Oracle confidence score',
    labels: ['oracle'],
  },

  // Operator metrics
  operator_tasks_total: {
    name: 'delta9_operator_tasks_total',
    type: 'counter' as const,
    description: 'Total operator tasks',
    labels: ['operator', 'status'],
  },
  operator_active: {
    name: 'delta9_operator_active',
    type: 'gauge' as const,
    description: 'Active operators',
  },

  // Validator metrics
  validations_total: {
    name: 'delta9_validations_total',
    type: 'counter' as const,
    description: 'Total validations',
    labels: ['status'],
  },
  validation_duration_seconds: {
    name: 'delta9_validation_duration_seconds',
    type: 'histogram' as const,
    description: 'Validation duration',
    unit: 'seconds',
  },

  // Cost metrics
  tokens_total: {
    name: 'delta9_tokens_total',
    type: 'counter' as const,
    description: 'Total tokens used',
    labels: ['model', 'type'],
  },
  cost_total_usd: {
    name: 'delta9_cost_total_usd',
    type: 'counter' as const,
    description: 'Total cost in USD',
    labels: ['model'],
  },
  budget_remaining_usd: {
    name: 'delta9_budget_remaining_usd',
    type: 'gauge' as const,
    description: 'Remaining budget in USD',
  },

  // Error metrics
  errors_total: {
    name: 'delta9_errors_total',
    type: 'counter' as const,
    description: 'Total errors',
    labels: ['type', 'component'],
  },

  // Legion metrics
  legion_strikes_total: {
    name: 'delta9_legion_strikes_total',
    type: 'counter' as const,
    description: 'Total legion strikes',
    labels: ['status'],
  },
  legion_operators_active: {
    name: 'delta9_legion_operators_active',
    type: 'gauge' as const,
    description: 'Active legion operators',
  },
  legion_conflicts_total: {
    name: 'delta9_legion_conflicts_total',
    type: 'counter' as const,
    description: 'Total legion conflicts detected',
    labels: ['type'],
  },

  // Webhook metrics
  webhooks_delivered_total: {
    name: 'delta9_webhooks_delivered_total',
    type: 'counter' as const,
    description: 'Total webhooks delivered',
    labels: ['status', 'format'],
  },
  webhook_latency_seconds: {
    name: 'delta9_webhook_latency_seconds',
    type: 'histogram' as const,
    description: 'Webhook delivery latency',
    unit: 'seconds',
  },

  // Plugin metrics
  plugins_active: {
    name: 'delta9_plugins_active',
    type: 'gauge' as const,
    description: 'Active plugins',
  },
  plugin_hooks_executed_total: {
    name: 'delta9_plugin_hooks_executed_total',
    type: 'counter' as const,
    description: 'Plugin hooks executed',
    labels: ['plugin', 'event'],
  },
}

export type Delta9MetricName = keyof typeof DELTA9_METRICS

// =============================================================================
// Time Series
// =============================================================================

export interface TimeSeriesPoint {
  timestamp: string
  value: number
  labels?: Record<string, string>
}

export interface TimeSeries {
  metric: string
  points: TimeSeriesPoint[]
  resolution: '1m' | '5m' | '15m' | '1h' | '1d'
  startTime: string
  endTime: string
}

// =============================================================================
// Dashboard Types
// =============================================================================

export interface DashboardPanel {
  id: string
  title: string
  type: 'chart' | 'stat' | 'table' | 'gauge' | 'heatmap'
  metrics: string[]
  config: PanelConfig
}

export interface PanelConfig {
  /** Chart type for chart panels */
  chartType?: 'line' | 'bar' | 'area' | 'stacked'
  /** Time range */
  timeRange?: string
  /** Refresh interval in seconds */
  refreshInterval?: number
  /** Aggregation function */
  aggregation?: 'sum' | 'avg' | 'min' | 'max' | 'count'
  /** Group by labels */
  groupBy?: string[]
  /** Thresholds for gauge/stat */
  thresholds?: Array<{ value: number; color: string }>
  /** Unit formatting */
  unit?: string
  /** Decimal places */
  decimals?: number
}

export interface Dashboard {
  id: string
  name: string
  description?: string
  panels: DashboardPanel[]
  layout: DashboardLayout
  createdAt: string
  updatedAt: string
}

export interface DashboardLayout {
  columns: number
  rows: Array<{
    height: number
    panels: Array<{
      panelId: string
      width: number
    }>
  }>
}

// =============================================================================
// Alerts
// =============================================================================

export type AlertSeverity = 'info' | 'warning' | 'critical'
export type AlertState = 'pending' | 'firing' | 'resolved'

export interface AlertRule {
  id: string
  name: string
  description?: string
  metric: string
  condition: AlertCondition
  severity: AlertSeverity
  labels?: Record<string, string>
  annotations?: Record<string, string>
  /** Evaluation interval in seconds */
  interval: number
  /** Duration before firing */
  for?: number
  enabled: boolean
}

export interface AlertCondition {
  operator: 'gt' | 'lt' | 'gte' | 'lte' | 'eq' | 'neq'
  threshold: number
  /** Label matchers */
  matchers?: Array<{
    label: string
    operator: '=' | '!=' | '=~' | '!~'
    value: string
  }>
}

export interface Alert {
  id: string
  ruleId: string
  state: AlertState
  severity: AlertSeverity
  value: number
  labels: Record<string, string>
  annotations: Record<string, string>
  startsAt?: string
  endsAt?: string
  lastEvaluation: string
}

// =============================================================================
// Snapshot
// =============================================================================

export interface MetricsSnapshot {
  timestamp: string
  metrics: Record<string, CounterValue | GaugeValue | HistogramValue | TimerValue>
  summary: {
    activeMissions: number
    activeTasks: number
    totalCost: number
    totalTokens: number
    errorRate: number
    avgTaskDuration: number
  }
}

// =============================================================================
// Export Format
// =============================================================================

export type ExportFormat = 'json' | 'prometheus' | 'csv'

export interface ExportOptions {
  format: ExportFormat
  metrics?: string[]
  startTime?: string
  endTime?: string
  labels?: Record<string, string>
}
