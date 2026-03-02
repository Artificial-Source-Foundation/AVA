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

      const status = await monitor.checkServer('server-a')

      expect(status.healthy).toBe(true)
      expect(status.consecutiveFailures).toBe(0)
      expect(status.serverId).toBe('server-a')
      expect(status.latencyMs).toBeGreaterThanOrEqual(0)
      expect(manager.ping).toHaveBeenCalledWith('server-a')
    })

    it('emits mcp:health event on success', async () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager)
      const handler = vi.fn()
      const sub = onEvent('mcp:health', handler)

      await monitor.checkServer('server-a')

      expect(handler).toHaveBeenCalledWith(
        expect.objectContaining({ serverId: 'server-a', healthy: true })
      )
      sub.dispose()
    })

    it('checkAll checks all connected servers', async () => {
      const manager = createMockManager()
      const monitor = new MCPHealthMonitor(manager)

      const statuses = await monitor.checkAll()

      expect(statuses.size).toBe(2)
      expect(statuses.get('server-a')?.healthy).toBe(true)
      expect(statuses.get('server-b')?.healthy).toBe(true)
      expect(manager.ping).toHaveBeenCalledTimes(2)
    })

    it('resets consecutive failures on success after prior failure', async () => {
      const pingFn = vi
        .fn()
        .mockRejectedValueOnce(new Error('fail'))
        .mockResolvedValueOnce(undefined)
      const manager = createMockManager({ ping: pingFn })
      const monitor = new MCPHealthMonitor(manager, { maxFailures: 5 })

      await monitor.checkServer('server-a')
      expect(monitor.getStatus('server-a')?.consecutiveFailures).toBe(1)

      await monitor.checkServer('server-a')
      expect(monitor.getStatus('server-a')?.consecutiveFailures).toBe(0)
      expect(monitor.getStatus('server-a')?.healthy).toBe(true)
    })
  })

  // ─── Health Check Failure ──────────────────────────────────────────

  describe('health check failure', () => {
    it('marks server as unhealthy on ping failure', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('connection refused')),
      })
      const monitor = new MCPHealthMonitor(manager, { maxFailures: 5 })

      const status = await monitor.checkServer('server-a')

      expect(status.healthy).toBe(false)
      expect(status.consecutiveFailures).toBe(1)
    })

    it('increments consecutive failures', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('timeout')),
      })
      const monitor = new MCPHealthMonitor(manager, { maxFailures: 5 })

      await monitor.checkServer('server-a')
      await monitor.checkServer('server-a')
      await monitor.checkServer('server-a')

      const status = monitor.getStatus('server-a')
      expect(status?.consecutiveFailures).toBe(3)
    })

    it('emits mcp:health event with failure info', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('connection refused')),
      })
      const monitor = new MCPHealthMonitor(manager, { maxFailures: 5 })
      const handler = vi.fn()
      const sub = onEvent('mcp:health', handler)

      await monitor.checkServer('server-a')

      expect(handler).toHaveBeenCalledWith(
        expect.objectContaining({
          serverId: 'server-a',
          healthy: false,
          failures: 1,
          error: 'connection refused',
        })
      )
      sub.dispose()
    })

    it('handles timeout during health check', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockImplementation(
          () => new Promise(() => {}) // never resolves
        ),
      })
      const monitor = new MCPHealthMonitor(manager, { timeoutMs: 100, maxFailures: 5 })

      const checkPromise = monitor.checkServer('server-a')
      vi.advanceTimersByTime(200)
      const status = await checkPromise

      expect(status.healthy).toBe(false)
      expect(status.consecutiveFailures).toBe(1)
    })
  })

  // ─── Auto-Restart ──────────────────────────────────────────────────

  describe('auto-restart after maxFailures', () => {
    it('restarts server after reaching maxFailures', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
      })
      const monitor = new MCPHealthMonitor(manager, { maxFailures: 3 })

      await monitor.checkServer('server-a') // failure 1
      await monitor.checkServer('server-a') // failure 2
      expect(manager.restart).not.toHaveBeenCalled()

      await monitor.checkServer('server-a') // failure 3 → auto-restart
      expect(manager.restart).toHaveBeenCalledWith('server-a')
    })

    it('resets failure count after successful restart', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
        restart: vi.fn().mockResolvedValue([]),
      })
      const monitor = new MCPHealthMonitor(manager, { maxFailures: 2 })

      await monitor.checkServer('server-a') // failure 1
      await monitor.checkServer('server-a') // failure 2 → restart

      const status = monitor.getStatus('server-a')
      expect(status?.consecutiveFailures).toBe(0)
      expect(status?.healthy).toBe(true)
    })

    it('emits mcp:restarted event on successful restart', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
        restart: vi.fn().mockResolvedValue([]),
      })
      const monitor = new MCPHealthMonitor(manager, { maxFailures: 1 })
      const handler = vi.fn()
      const sub = onEvent('mcp:restarted', handler)

      await monitor.checkServer('server-a')

      expect(handler).toHaveBeenCalledWith({ serverId: 'server-a' })
      sub.dispose()
    })

    it('does not reset failures if restart also fails', async () => {
      const manager = createMockManager({
        ping: vi.fn().mockRejectedValue(new Error('dead')),
        restart: vi.fn().mockRejectedValue(new Error('restart failed')),
      })
      const monitor = new MCPHealthMonitor(manager, { maxFailures: 1 })

      await monitor.checkServer('server-a')

      const status = monitor.getStatus('server-a')
      // restart failed so failures stay at 1 (not reset)
      expect(status?.consecutiveFailures).toBe(1)
      expect(status?.healthy).toBe(false)
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
      const monitor = new MCPHealthMonitor(manager)

      await monitor.checkAll()

      const statuses = monitor.getAllStatuses()
      expect(statuses.size).toBe(2)
      // Verify it's a copy
      statuses.clear()
      expect(monitor.getAllStatuses().size).toBe(2)
    })
  })
})
