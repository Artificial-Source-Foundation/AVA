/**
 * Metrics Collector
 *
 * Collects, aggregates, and exports metrics.
 */

import type {
  CounterValue,
  GaugeValue,
  HistogramValue,
  TimerValue,
  TimeSeries,
  TimeSeriesPoint,
  AlertRule,
  Alert,
  MetricsSnapshot,
  ExportOptions,
} from './types.js'
import { DELTA9_METRICS } from './types.js'

// =============================================================================
// Histogram Buckets
// =============================================================================

const DEFAULT_BUCKETS = [0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10]
const DURATION_BUCKETS = [0.1, 0.5, 1, 2, 5, 10, 30, 60, 120, 300, 600]

// =============================================================================
// Metrics Collector
// =============================================================================

export class MetricsCollector {
  private counters: Map<string, Map<string, number>> = new Map()
  private gauges: Map<string, Map<string, number>> = new Map()
  private histograms: Map<
    string,
    Map<string, { sum: number; count: number; buckets: Map<number, number> }>
  > = new Map()
  private timers: Map<string, Map<string, number[]>> = new Map()

  private timeSeries: Map<string, TimeSeriesPoint[]> = new Map()
  private maxTimeSeriesPoints = 10000

  private alertRules: Map<string, AlertRule> = new Map()
  private alerts: Map<string, Alert> = new Map()
  private alertHandlers: Array<(alert: Alert) => void> = []

  // ===========================================================================
  // Counter Operations
  // ===========================================================================

  /**
   * Increment a counter
   */
  increment(name: string, labels: Record<string, string> = {}, value = 1): void {
    const key = this.labelsToKey(labels)
    const metric = this.counters.get(name) || new Map()

    metric.set(key, (metric.get(key) || 0) + value)
    this.counters.set(name, metric)

    this.recordTimeSeries(name, metric.get(key)!, labels)
    this.checkAlerts(name, metric.get(key)!, labels)
  }

  /**
   * Get counter value
   */
  getCounter(name: string, labels: Record<string, string> = {}): number {
    const key = this.labelsToKey(labels)
    return this.counters.get(name)?.get(key) || 0
  }

  // ===========================================================================
  // Gauge Operations
  // ===========================================================================

  /**
   * Set a gauge value
   */
  set(name: string, value: number, labels: Record<string, string> = {}): void {
    const key = this.labelsToKey(labels)
    const metric = this.gauges.get(name) || new Map()

    metric.set(key, value)
    this.gauges.set(name, metric)

    this.recordTimeSeries(name, value, labels)
    this.checkAlerts(name, value, labels)
  }

  /**
   * Increment a gauge
   */
  incGauge(name: string, labels: Record<string, string> = {}, value = 1): void {
    const current = this.getGauge(name, labels)
    this.set(name, current + value, labels)
  }

  /**
   * Decrement a gauge
   */
  decGauge(name: string, labels: Record<string, string> = {}, value = 1): void {
    const current = this.getGauge(name, labels)
    this.set(name, current - value, labels)
  }

  /**
   * Get gauge value
   */
  getGauge(name: string, labels: Record<string, string> = {}): number {
    const key = this.labelsToKey(labels)
    return this.gauges.get(name)?.get(key) || 0
  }

  // ===========================================================================
  // Histogram Operations
  // ===========================================================================

  /**
   * Observe a histogram value
   */
  observe(
    name: string,
    value: number,
    labels: Record<string, string> = {},
    buckets = DEFAULT_BUCKETS
  ): void {
    const key = this.labelsToKey(labels)
    const metric = this.histograms.get(name) || new Map()

    let histogram = metric.get(key)
    if (!histogram) {
      histogram = {
        sum: 0,
        count: 0,
        buckets: new Map(buckets.map((b) => [b, 0])),
      }
      metric.set(key, histogram)
    }

    histogram.sum += value
    histogram.count++

    for (const [bucket, count] of histogram.buckets) {
      if (value <= bucket) {
        histogram.buckets.set(bucket, count + 1)
      }
    }

    this.histograms.set(name, metric)
    this.recordTimeSeries(name, value, labels)
    this.checkAlerts(name, value, labels)
  }

  /**
   * Get histogram value
   */
  getHistogram(name: string, labels: Record<string, string> = {}): HistogramValue | null {
    const key = this.labelsToKey(labels)
    const histogram = this.histograms.get(name)?.get(key)

    if (!histogram) return null

    return {
      name,
      type: 'histogram',
      value: histogram.count > 0 ? histogram.sum / histogram.count : 0,
      timestamp: new Date().toISOString(),
      labels,
      buckets: Array.from(histogram.buckets.entries()).map(([le, count]) => ({ le, count })),
      sum: histogram.sum,
      count: histogram.count,
    }
  }

  // ===========================================================================
  // Timer Operations
  // ===========================================================================

  /**
   * Start a timer
   */
  startTimer(name: string, labels: Record<string, string> = {}): () => number {
    const start = performance.now()

    return () => {
      const duration = (performance.now() - start) / 1000 // Convert to seconds
      this.recordTime(name, duration, labels)
      return duration
    }
  }

  /**
   * Record a timer value
   */
  recordTime(name: string, duration: number, labels: Record<string, string> = {}): void {
    const key = this.labelsToKey(labels)
    const metric = this.timers.get(name) || new Map()

    const times = metric.get(key) || []
    times.push(duration)

    // Keep last 1000 values
    if (times.length > 1000) {
      times.shift()
    }

    metric.set(key, times)
    this.timers.set(name, metric)

    // Also record as histogram for bucket distribution
    this.observe(`${name}_histogram`, duration, labels, DURATION_BUCKETS)

    this.recordTimeSeries(name, duration, labels)
    this.checkAlerts(name, duration, labels)
  }

  /**
   * Get timer stats
   */
  getTimer(name: string, labels: Record<string, string> = {}): TimerValue | null {
    const key = this.labelsToKey(labels)
    const times = this.timers.get(name)?.get(key)

    if (!times || times.length === 0) return null

    const sorted = [...times].sort((a, b) => a - b)
    const sum = times.reduce((a, b) => a + b, 0)

    return {
      name,
      type: 'timer',
      value: sum / times.length,
      timestamp: new Date().toISOString(),
      labels,
      min: sorted[0],
      max: sorted[sorted.length - 1],
      avg: sum / times.length,
      percentiles: {
        p50: this.percentile(sorted, 50),
        p90: this.percentile(sorted, 90),
        p95: this.percentile(sorted, 95),
        p99: this.percentile(sorted, 99),
      },
    }
  }

  private percentile(sorted: number[], p: number): number {
    const index = Math.ceil((p / 100) * sorted.length) - 1
    return sorted[Math.max(0, index)]
  }

  // ===========================================================================
  // Time Series
  // ===========================================================================

  private recordTimeSeries(name: string, value: number, labels: Record<string, string>): void {
    const points = this.timeSeries.get(name) || []

    points.push({
      timestamp: new Date().toISOString(),
      value,
      labels,
    })

    // Trim to max points
    if (points.length > this.maxTimeSeriesPoints) {
      points.splice(0, points.length - this.maxTimeSeriesPoints)
    }

    this.timeSeries.set(name, points)
  }

  /**
   * Get time series data
   */
  getTimeSeries(
    name: string,
    startTime?: string,
    endTime?: string,
    resolution: '1m' | '5m' | '15m' | '1h' | '1d' = '5m'
  ): TimeSeries {
    const points = this.timeSeries.get(name) || []
    const start = startTime ? new Date(startTime) : new Date(Date.now() - 3600000)
    const end = endTime ? new Date(endTime) : new Date()

    const filtered = points.filter((p) => {
      const ts = new Date(p.timestamp)
      return ts >= start && ts <= end
    })

    // Aggregate by resolution
    const aggregated = this.aggregateTimeSeries(filtered, resolution)

    return {
      metric: name,
      points: aggregated,
      resolution,
      startTime: start.toISOString(),
      endTime: end.toISOString(),
    }
  }

  private aggregateTimeSeries(points: TimeSeriesPoint[], resolution: string): TimeSeriesPoint[] {
    if (points.length === 0) return []

    const bucketMs = this.resolutionToMs(resolution)
    const buckets = new Map<
      number,
      { sum: number; count: number; labels?: Record<string, string> }
    >()

    for (const point of points) {
      const ts = new Date(point.timestamp).getTime()
      const bucket = Math.floor(ts / bucketMs) * bucketMs

      const existing = buckets.get(bucket) || { sum: 0, count: 0, labels: point.labels }
      existing.sum += point.value
      existing.count++
      buckets.set(bucket, existing)
    }

    return Array.from(buckets.entries())
      .sort(([a], [b]) => a - b)
      .map(([ts, data]) => ({
        timestamp: new Date(ts).toISOString(),
        value: data.sum / data.count,
        labels: data.labels,
      }))
  }

  private resolutionToMs(resolution: string): number {
    switch (resolution) {
      case '1m':
        return 60000
      case '5m':
        return 300000
      case '15m':
        return 900000
      case '1h':
        return 3600000
      case '1d':
        return 86400000
      default:
        return 300000
    }
  }

  // ===========================================================================
  // Alerts
  // ===========================================================================

  /**
   * Add an alert rule
   */
  addAlertRule(rule: AlertRule): void {
    this.alertRules.set(rule.id, rule)
  }

  /**
   * Remove an alert rule
   */
  removeAlertRule(ruleId: string): boolean {
    return this.alertRules.delete(ruleId)
  }

  /**
   * Check alerts for a metric
   */
  private checkAlerts(name: string, value: number, labels: Record<string, string>): void {
    for (const rule of this.alertRules.values()) {
      if (rule.metric !== name || !rule.enabled) continue

      // Check label matchers
      if (rule.condition.matchers) {
        let matches = true
        for (const matcher of rule.condition.matchers) {
          const labelValue = labels[matcher.label] || ''
          switch (matcher.operator) {
            case '=':
              matches = matches && labelValue === matcher.value
              break
            case '!=':
              matches = matches && labelValue !== matcher.value
              break
            case '=~':
              matches = matches && new RegExp(matcher.value).test(labelValue)
              break
            case '!~':
              matches = matches && !new RegExp(matcher.value).test(labelValue)
              break
          }
        }
        if (!matches) continue
      }

      // Check condition
      let firing = false
      switch (rule.condition.operator) {
        case 'gt':
          firing = value > rule.condition.threshold
          break
        case 'lt':
          firing = value < rule.condition.threshold
          break
        case 'gte':
          firing = value >= rule.condition.threshold
          break
        case 'lte':
          firing = value <= rule.condition.threshold
          break
        case 'eq':
          firing = value === rule.condition.threshold
          break
        case 'neq':
          firing = value !== rule.condition.threshold
          break
      }

      const alertKey = `${rule.id}:${this.labelsToKey(labels)}`
      const existing = this.alerts.get(alertKey)

      if (firing) {
        if (!existing || existing.state === 'resolved') {
          const alert: Alert = {
            id: alertKey,
            ruleId: rule.id,
            state: 'pending',
            severity: rule.severity,
            value,
            labels: { ...labels, ...rule.labels },
            annotations: rule.annotations || {},
            startsAt: new Date().toISOString(),
            lastEvaluation: new Date().toISOString(),
          }

          // If no 'for' duration, fire immediately
          if (!rule.for) {
            alert.state = 'firing'
          }

          this.alerts.set(alertKey, alert)
          this.notifyAlert(alert)
        } else if (existing.state === 'pending') {
          // Check if pending duration exceeded
          const pendingDuration = Date.now() - new Date(existing.startsAt!).getTime()
          if (rule.for && pendingDuration >= rule.for * 1000) {
            existing.state = 'firing'
            existing.lastEvaluation = new Date().toISOString()
            this.notifyAlert(existing)
          }
        } else {
          existing.value = value
          existing.lastEvaluation = new Date().toISOString()
        }
      } else if (existing && existing.state !== 'resolved') {
        existing.state = 'resolved'
        existing.endsAt = new Date().toISOString()
        existing.lastEvaluation = new Date().toISOString()
        this.notifyAlert(existing)
      }
    }
  }

  private notifyAlert(alert: Alert): void {
    for (const handler of this.alertHandlers) {
      try {
        handler(alert)
      } catch {
        // Ignore handler errors
      }
    }
  }

  onAlert(handler: (alert: Alert) => void): () => void {
    this.alertHandlers.push(handler)
    return () => {
      const index = this.alertHandlers.indexOf(handler)
      if (index >= 0) {
        this.alertHandlers.splice(index, 1)
      }
    }
  }

  /**
   * Get active alerts
   */
  getActiveAlerts(): Alert[] {
    return Array.from(this.alerts.values()).filter(
      (a) => a.state === 'firing' || a.state === 'pending'
    )
  }

  /**
   * Get all alerts
   */
  getAllAlerts(): Alert[] {
    return Array.from(this.alerts.values())
  }

  // ===========================================================================
  // Snapshot
  // ===========================================================================

  /**
   * Get current metrics snapshot
   */
  getSnapshot(): MetricsSnapshot {
    const metrics: Record<string, CounterValue | GaugeValue | HistogramValue | TimerValue> = {}

    // Counters
    for (const [name, values] of this.counters) {
      for (const [key, value] of values) {
        const labels = this.keyToLabels(key)
        metrics[`${name}${key}`] = {
          name,
          type: 'counter' as const,
          value,
          timestamp: new Date().toISOString(),
          labels,
        }
      }
    }

    // Gauges
    for (const [name, values] of this.gauges) {
      for (const [key, value] of values) {
        const labels = this.keyToLabels(key)
        metrics[`${name}${key}`] = {
          name,
          type: 'gauge' as const,
          value,
          timestamp: new Date().toISOString(),
          labels,
        }
      }
    }

    // Summary
    const activeMissions = this.getGauge(DELTA9_METRICS.missions_active.name)
    const activeTasks = this.getGauge(DELTA9_METRICS.tasks_active.name)
    const totalCost = this.getCounter(DELTA9_METRICS.cost_total_usd.name)
    const totalTokens = this.getCounter(DELTA9_METRICS.tokens_total.name)

    const tasksTotal = this.getCounter(DELTA9_METRICS.tasks_total.name)
    const tasksFailed = this.getCounter(DELTA9_METRICS.tasks_total.name, { status: 'failed' })
    const errorRate = tasksTotal > 0 ? tasksFailed / tasksTotal : 0

    const taskTimer = this.getTimer(DELTA9_METRICS.task_duration_seconds.name)
    const avgTaskDuration = taskTimer?.avg || 0

    return {
      timestamp: new Date().toISOString(),
      metrics,
      summary: {
        activeMissions,
        activeTasks,
        totalCost,
        totalTokens,
        errorRate,
        avgTaskDuration,
      },
    }
  }

  // ===========================================================================
  // Export
  // ===========================================================================

  /**
   * Export metrics in various formats
   */
  export(options: ExportOptions): string {
    switch (options.format) {
      case 'prometheus':
        return this.exportPrometheus(options)
      case 'json':
        return this.exportJson(options)
      case 'csv':
        return this.exportCsv(options)
      default:
        return this.exportJson(options)
    }
  }

  private exportPrometheus(options: ExportOptions): string {
    const lines: string[] = []

    // Counters
    for (const [name, values] of this.counters) {
      if (options.metrics && !options.metrics.includes(name)) continue

      lines.push(`# TYPE ${name} counter`)
      for (const [key, value] of values) {
        const labels = this.keyToLabels(key)
        if (options.labels && !this.matchLabels(labels, options.labels)) continue

        const labelStr = this.labelsToPrometheus(labels)
        lines.push(`${name}${labelStr} ${value}`)
      }
    }

    // Gauges
    for (const [name, values] of this.gauges) {
      if (options.metrics && !options.metrics.includes(name)) continue

      lines.push(`# TYPE ${name} gauge`)
      for (const [key, value] of values) {
        const labels = this.keyToLabels(key)
        if (options.labels && !this.matchLabels(labels, options.labels)) continue

        const labelStr = this.labelsToPrometheus(labels)
        lines.push(`${name}${labelStr} ${value}`)
      }
    }

    // Histograms
    for (const [name, values] of this.histograms) {
      if (options.metrics && !options.metrics.includes(name)) continue

      lines.push(`# TYPE ${name} histogram`)
      for (const [key, histogram] of values) {
        const labels = this.keyToLabels(key)
        if (options.labels && !this.matchLabels(labels, options.labels)) continue

        for (const [bucket, count] of histogram.buckets) {
          const bucketLabels = { ...labels, le: String(bucket) }
          lines.push(`${name}_bucket${this.labelsToPrometheus(bucketLabels)} ${count}`)
        }

        const labelStr = this.labelsToPrometheus(labels)
        lines.push(`${name}_sum${labelStr} ${histogram.sum}`)
        lines.push(`${name}_count${labelStr} ${histogram.count}`)
      }
    }

    return lines.join('\n')
  }

  private exportJson(options: ExportOptions): string {
    const snapshot = this.getSnapshot()

    if (options.metrics) {
      const filtered: Record<string, CounterValue | GaugeValue | HistogramValue | TimerValue> = {}
      for (const [key, value] of Object.entries(snapshot.metrics)) {
        if (options.metrics.some((m) => key.startsWith(m))) {
          filtered[key] = value
        }
      }
      snapshot.metrics = filtered
    }

    return JSON.stringify(snapshot, null, 2)
  }

  private exportCsv(options: ExportOptions): string {
    const lines: string[] = ['metric,value,timestamp,labels']

    for (const [name, values] of this.counters) {
      if (options.metrics && !options.metrics.includes(name)) continue

      for (const [key, value] of values) {
        const labels = this.keyToLabels(key)
        if (options.labels && !this.matchLabels(labels, options.labels)) continue

        lines.push(`${name},${value},${new Date().toISOString()},"${JSON.stringify(labels)}"`)
      }
    }

    for (const [name, values] of this.gauges) {
      if (options.metrics && !options.metrics.includes(name)) continue

      for (const [key, value] of values) {
        const labels = this.keyToLabels(key)
        if (options.labels && !this.matchLabels(labels, options.labels)) continue

        lines.push(`${name},${value},${new Date().toISOString()},"${JSON.stringify(labels)}"`)
      }
    }

    return lines.join('\n')
  }

  private matchLabels(labels: Record<string, string>, filter: Record<string, string>): boolean {
    for (const [key, value] of Object.entries(filter)) {
      if (labels[key] !== value) return false
    }
    return true
  }

  // ===========================================================================
  // Utilities
  // ===========================================================================

  private labelsToKey(labels: Record<string, string>): string {
    const sorted = Object.entries(labels).sort(([a], [b]) => a.localeCompare(b))
    return sorted.map(([k, v]) => `${k}=${v}`).join(',')
  }

  private keyToLabels(key: string): Record<string, string> {
    if (!key) return {}
    const labels: Record<string, string> = {}
    for (const pair of key.split(',')) {
      const [k, v] = pair.split('=')
      if (k && v !== undefined) labels[k] = v
    }
    return labels
  }

  private labelsToPrometheus(labels: Record<string, string>): string {
    const pairs = Object.entries(labels)
    if (pairs.length === 0) return ''
    return `{${pairs.map(([k, v]) => `${k}="${v}"`).join(',')}}`
  }

  /**
   * Reset all metrics
   */
  reset(): void {
    this.counters.clear()
    this.gauges.clear()
    this.histograms.clear()
    this.timers.clear()
    this.timeSeries.clear()
    this.alerts.clear()
  }
}
