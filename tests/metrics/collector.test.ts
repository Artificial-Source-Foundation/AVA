/**
 * Metrics Collector Tests
 */

import { describe, it, expect, beforeEach } from 'vitest'
import { MetricsCollector } from '../../src/metrics/index.js'

describe('MetricsCollector', () => {
  let collector: MetricsCollector

  beforeEach(() => {
    collector = new MetricsCollector()
  })

  describe('counters', () => {
    it('should increment counter', () => {
      collector.increment('test_counter')
      expect(collector.getCounter('test_counter')).toBe(1)
    })

    it('should increment counter by specific value', () => {
      collector.increment('test_counter', {}, 5)
      expect(collector.getCounter('test_counter')).toBe(5)
    })

    it('should handle labels', () => {
      collector.increment('test_counter', { status: 'success' })
      collector.increment('test_counter', { status: 'failure' })

      expect(collector.getCounter('test_counter', { status: 'success' })).toBe(1)
      expect(collector.getCounter('test_counter', { status: 'failure' })).toBe(1)
    })

    it('should accumulate increments', () => {
      collector.increment('test_counter')
      collector.increment('test_counter')
      collector.increment('test_counter')

      expect(collector.getCounter('test_counter')).toBe(3)
    })
  })

  describe('gauges', () => {
    it('should set gauge value', () => {
      collector.set('test_gauge', 42)
      expect(collector.getGauge('test_gauge')).toBe(42)
    })

    it('should overwrite gauge value', () => {
      collector.set('test_gauge', 10)
      collector.set('test_gauge', 20)
      expect(collector.getGauge('test_gauge')).toBe(20)
    })

    it('should increment gauge', () => {
      collector.set('test_gauge', 10)
      collector.incGauge('test_gauge')
      expect(collector.getGauge('test_gauge')).toBe(11)
    })

    it('should decrement gauge', () => {
      collector.set('test_gauge', 10)
      collector.decGauge('test_gauge')
      expect(collector.getGauge('test_gauge')).toBe(9)
    })

    it('should handle labels', () => {
      collector.set('test_gauge', 100, { region: 'us' })
      collector.set('test_gauge', 200, { region: 'eu' })

      expect(collector.getGauge('test_gauge', { region: 'us' })).toBe(100)
      expect(collector.getGauge('test_gauge', { region: 'eu' })).toBe(200)
    })
  })

  describe('histograms', () => {
    it('should observe histogram value', () => {
      collector.observe('test_histogram', 0.5)
      const histogram = collector.getHistogram('test_histogram')

      expect(histogram).toBeDefined()
      expect(histogram?.count).toBe(1)
      expect(histogram?.sum).toBe(0.5)
    })

    it('should calculate bucket counts', () => {
      collector.observe('response_time', 0.1)
      collector.observe('response_time', 0.5)
      collector.observe('response_time', 1.5)
      collector.observe('response_time', 3.0)

      const histogram = collector.getHistogram('response_time')
      expect(histogram?.count).toBe(4)
    })

    it('should handle multiple observations', () => {
      for (let i = 0; i < 100; i++) {
        collector.observe('latency', Math.random() * 10)
      }

      const histogram = collector.getHistogram('latency')
      expect(histogram?.count).toBe(100)
      expect(histogram?.sum).toBeGreaterThan(0)
    })
  })

  describe('timers', () => {
    it('should record timer values', () => {
      collector.recordTime('test_timer', 1.5)
      collector.recordTime('test_timer', 2.5)
      collector.recordTime('test_timer', 3.5)

      const timer = collector.getTimer('test_timer')
      expect(timer?.min).toBe(1.5)
      expect(timer?.max).toBe(3.5)
      expect(timer?.avg).toBe(2.5)
    })

    it('should calculate percentiles', () => {
      for (let i = 1; i <= 100; i++) {
        collector.recordTime('percentile_test', i)
      }

      const timer = collector.getTimer('percentile_test')
      expect(timer?.percentiles.p50).toBeGreaterThanOrEqual(50)
      expect(timer?.percentiles.p90).toBeGreaterThanOrEqual(90)
      expect(timer?.percentiles.p99).toBeGreaterThanOrEqual(99)
    })

    it('should use startTimer helper', async () => {
      const stopTimer = collector.startTimer('async_operation')
      await new Promise(resolve => setTimeout(resolve, 10))
      const duration = stopTimer()

      expect(duration).toBeGreaterThan(0)
      const timer = collector.getTimer('async_operation')
      expect(timer).toBeDefined()
    })
  })

  describe('time series', () => {
    it('should record time series data', () => {
      collector.increment('events')
      collector.increment('events')
      collector.increment('events')

      const series = collector.getTimeSeries('events')
      expect(series.points.length).toBeGreaterThan(0)
    })

    it('should aggregate time series', () => {
      for (let i = 0; i < 10; i++) {
        collector.set('load', Math.random() * 100)
      }

      const series = collector.getTimeSeries('load', undefined, undefined, '1m')
      expect(series.resolution).toBe('1m')
    })
  })

  describe('alerts', () => {
    it('should trigger alert on threshold exceeded', () => {
      const alerts: unknown[] = []
      collector.onAlert(alert => alerts.push(alert))

      collector.addAlertRule({
        id: 'high_errors',
        name: 'High Error Rate',
        metric: 'error_count',
        condition: { operator: 'gt', threshold: 10 },
        severity: 'critical',
        interval: 60,
        enabled: true,
      })

      // Increment beyond threshold
      collector.increment('error_count', {}, 15)

      expect(alerts.length).toBeGreaterThan(0)
    })

    it('should resolve alert when threshold not exceeded', () => {
      const alerts: unknown[] = []
      collector.onAlert(alert => alerts.push(alert))

      collector.addAlertRule({
        id: 'low_memory',
        name: 'Low Memory',
        metric: 'memory_usage',
        condition: { operator: 'gt', threshold: 80 },
        severity: 'warning',
        interval: 60,
        enabled: true,
      })

      collector.set('memory_usage', 90) // Trigger
      collector.set('memory_usage', 50) // Resolve

      const activeAlerts = collector.getActiveAlerts()
      expect(activeAlerts.filter(a => a.state === 'firing')).toHaveLength(0)
    })

    it('should remove alert rule', () => {
      collector.addAlertRule({
        id: 'test_rule',
        name: 'Test',
        metric: 'test',
        condition: { operator: 'gt', threshold: 10 },
        severity: 'info',
        interval: 60,
        enabled: true,
      })

      expect(collector.removeAlertRule('test_rule')).toBe(true)
      expect(collector.removeAlertRule('nonexistent')).toBe(false)
    })
  })

  describe('snapshot', () => {
    it('should return metrics snapshot', () => {
      collector.increment('tasks_total', { status: 'completed' }, 10)
      collector.set('active_tasks', 5)
      collector.recordTime('task_duration', 2.5)

      const snapshot = collector.getSnapshot()

      expect(snapshot.timestamp).toBeDefined()
      expect(snapshot.metrics).toBeDefined()
      expect(snapshot.summary).toBeDefined()
    })
  })

  describe('export', () => {
    it('should export as JSON', () => {
      collector.increment('test', {}, 5)

      const json = collector.export({ format: 'json' })
      expect(() => JSON.parse(json)).not.toThrow()
    })

    it('should export as Prometheus format', () => {
      collector.increment('http_requests', { method: 'GET' }, 100)
      collector.set('temperature', 72)

      const prometheus = collector.export({ format: 'prometheus' })
      expect(prometheus).toContain('TYPE')
      expect(prometheus).toContain('http_requests')
    })

    it('should export as CSV', () => {
      collector.increment('events', {}, 10)

      const csv = collector.export({ format: 'csv' })
      expect(csv).toContain('metric,value,timestamp,labels')
    })

    it('should filter metrics on export', () => {
      collector.increment('keep_me', {}, 5)
      collector.increment('skip_me', {}, 10)

      const json = collector.export({ format: 'json', metrics: ['keep_me'] })
      const parsed = JSON.parse(json)

      expect(Object.keys(parsed.metrics).some(k => k.startsWith('keep_me'))).toBe(true)
    })
  })

  describe('reset', () => {
    it('should reset all metrics', () => {
      collector.increment('counter', {}, 100)
      collector.set('gauge', 50)
      collector.observe('histogram', 5)

      collector.reset()

      expect(collector.getCounter('counter')).toBe(0)
      expect(collector.getGauge('gauge')).toBe(0)
      expect(collector.getHistogram('histogram')).toBeNull()
    })
  })
})
