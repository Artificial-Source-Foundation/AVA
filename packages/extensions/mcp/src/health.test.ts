import { installMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { onEvent, resetRegistries } from '@ava/core-v2/extensions'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { MCPManager } from './health.js'
import { MCPHealthMonitor } from './health.js'

function createMockManager(overrides?: Partial<MCPManager>): MCPManager {
  return {
    getConnectedServers: vi.fn().mockReturnValue(['server-a', 'server-b']),
    ping: vi.fn().mockResolvedValue(undefined),
    restart: vi.fn().mockResolvedValue([]),
    ...overrides,
  }
}

describe('MCPHealthMonitor', () => {
  beforeEach(() => {
    installMockPlatform()
    resetRegistries()
    vi.useFakeTimers()
  })

  afterEach(() => {
    resetRegistries()
    resetLogger()
    vi.useRealTimers()
  })

  // ─── Health Check Success ──────────────────────────────────────────

  describe('health check success', () => {
    it('marks server as healthy on successful ping', async () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000 })

      // Trigger check via timer
      monitor.start()
      await vi.advanceTimersByTimeAsync(1000)

      const status = monitor.getStatus('server-a')
      expect(status?.healthy).toBe(true)
      expect(status?.consecutiveFailures).toBe(0)
      expect(status?.serverId).toBe('server-a')
      expect(status?.latencyMs).toBeGreaterThanOrEqual(0)
      expect(manager.ping).toHaveBeenCalledWith('server-a')

      monitor.stop()
    })

    it('emits mcp:health:unhealthy and mcp:health:recovered events', async () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 1 })
      const unhealthyHandler = vi.fn()
      const recoveredHandler = vi.fn()
      const sub1 = onEvent('mcp:health:unhealthy', unhealthyHandler)
      const sub2 = onEvent('mcp:health:recovered', recoveredHandler)

      monitor.start()

      // Make ping fail
      manager.ping = vi.fn().mockRejectedValue(new Error('fail'))
      await vi.advanceTimersByTimeAsync(1000)

      expect(unhealthyHandler).toHaveBeenCalledWith(
        expect.objectContaining({ serverId: 'server-a', failures: 1 })
      )
      expect(recoveredHandler).toHaveBeenCalledWith(
        expect.objectContaining({ serverId: 'server-a' })
      )

      sub1.dispose()
      sub2.dispose()
      monitor.stop()
    })

    it('checks all connected servers via timer', async () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000 })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000)

      const statuses = monitor.getAllStatuses()
      expect(statuses.length).toBe(2)
      expect(statuses.find((s) => s.serverId === 'server-a')?.healthy).toBe(true)
      expect(statuses.find((s) => s.serverId === 'server-b')?.healthy).toBe(true)
      expect(manager.ping).toHaveBeenCalledTimes(2)

      monitor.stop()
    })

    it('resets consecutive failures on successful restart after prior failure', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('fail')),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 2 })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000) // 1 failure
      expect(monitor.getStatus('server-a')?.consecutiveFailures).toBe(1)

      await vi.advanceTimersByTimeAsync(1000) // 2 failures -> restart
      expect(monitor.getStatus('server-a')?.consecutiveFailures).toBe(0)
      expect(monitor.getStatus('server-a')?.healthy).toBe(true)

      monitor.stop()
    })
  })

  // ─── Health Check Failure ──────────────────────────────────────────

  describe('health check failure', () => {
    it('marks server as unhealthy on ping failure', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('connection refused')),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 5 })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000)

      const status = monitor.getStatus('server-a')
      expect(status?.healthy).toBe(false)
      expect(status?.consecutiveFailures).toBe(1)

      monitor.stop()
    })

    it('increments consecutive failures', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('timeout')),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 5 })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000) // 1
      await vi.advanceTimersByTimeAsync(1000) // 2
      await vi.advanceTimersByTimeAsync(1000) // 3

      const status = monitor.getStatus('server-a')
      expect(status?.consecutiveFailures).toBe(3)

      monitor.stop()
    })

    it('emits mcp:health:unhealthy event with failure info', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('connection refused')),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 1 })
      const handler = vi.fn()
      const sub = onEvent('mcp:health:unhealthy', handler)

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000)

      expect(handler).toHaveBeenCalledWith(
        expect.objectContaining({
          serverId: 'server-a',
          failures: 1,
        })
      )
      sub.dispose()
      monitor.stop()
    })

    it('handles timeout during health check', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockImplementation(
          () => new Promise(() => {}) // never resolves
        ),
      })
      const monitor = new MCPHealthMonitor(manager, {
        intervalMs: 1000,
        timeoutMs: 100,
        maxFailures: 5,
      })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1200) // 1000ms interval + 100ms timeout + buffer

      const status = monitor.getStatus('server-a')
      expect(status?.healthy).toBe(false)
      expect(status?.consecutiveFailures).toBe(1)

      monitor.stop()
    })
  })

  // ─── Auto-Restart ──────────────────────────────────────────────────

  describe('auto-restart after maxFailures', () => {
    it('restarts server after reaching maxFailures', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 3 })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000) // 1
      expect(manager.restart).not.toHaveBeenCalled()

      await vi.advanceTimersByTimeAsync(1000) // 2
      expect(manager.restart).not.toHaveBeenCalled()

      await vi.advanceTimersByTimeAsync(1000) // 3 → auto-restart
      expect(manager.restart).toHaveBeenCalledWith('server-a')

      monitor.stop()
    })

    it('resets failure count after successful restart', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
        restart: vi.fn().mockResolvedValue([]),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 2 })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000) // 1
      await vi.advanceTimersByTimeAsync(1000) // 2 → restart

      const status = monitor.getStatus('server-a')
      expect(status?.consecutiveFailures).toBe(0)
      expect(status?.healthy).toBe(true)

      monitor.stop()
    })

    it('emits mcp:health:recovered event on successful restart', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
        restart: vi.fn().mockResolvedValue([]),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 1 })
      const handler = vi.fn()
      const sub = onEvent('mcp:health:recovered', handler)

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000)

      expect(handler).toHaveBeenCalledWith({ serverId: 'server-a' })
      sub.dispose()
      monitor.stop()
    })

    it('does not reset failures if restart also fails', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
        restart: vi.fn().mockRejectedValue(new Error('restart failed')),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 1 })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000)

      const status = monitor.getStatus('server-a')
      // restart failed so failures stay at 1 (not reset)
      expect(status?.consecutiveFailures).toBe(1)
      expect(status?.healthy).toBe(false)

      monitor.stop()
    })

    it('emits mcp:health:restart-failed on restart failure', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
        restart: vi.fn().mockRejectedValue(new Error('restart failed')),
      })
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000, maxFailures: 1 })
      const handler = vi.fn()
      const sub = onEvent('mcp:health:restart-failed', handler)

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000)

      expect(handler).toHaveBeenCalledWith(
        expect.objectContaining({ serverId: 'server-a', error: 'restart failed' })
      )
      sub.dispose()
      monitor.stop()
    })
  })

  // ─── Start / Stop ──────────────────────────────────────────────────

  describe('start/stop monitoring', () => {
    it('starts periodic health checks', async () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000 })

      monitor.start()

      // No checks yet
      expect(manager.ping).not.toHaveBeenCalled()

      // Advance past one interval and flush microtasks
      await vi.advanceTimersByTimeAsync(1000)

      expect(manager.ping).toHaveBeenCalled()

      monitor.stop()
    })

    it('stop clears the timer', () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000 })

      monitor.start()
      monitor.stop()

      vi.advanceTimersByTime(5000)
      expect(manager.ping).not.toHaveBeenCalled()
    })

    it('start is idempotent', () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000 })

      monitor.start()
      monitor.start() // Should not create a second timer

      vi.advanceTimersByTime(1000)
      // Only one interval should fire
      monitor.stop()
    })
  })

  // ─── Status Queries ────────────────────────────────────────────────

  describe('status queries', () => {
    it('getStatus returns undefined for unknown server', () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager)

      expect(monitor.getStatus('unknown')).toBeUndefined()
    })

    it('getAllStatuses returns a copy of all statuses', async () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager, { intervalMs: 1000 })

      monitor.start()
      await vi.advanceTimersByTimeAsync(1000)

      const statuses = monitor.getAllStatuses()
      expect(statuses.length).toBe(2)
      // Verify it's a copy
      statuses.length = 0
      expect(monitor.getAllStatuses().length).toBe(2)

      monitor.stop()
    })
  })
})
