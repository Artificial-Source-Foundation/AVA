/**
 * MCP server health monitoring.
 *
 * Periodically pings connected MCP servers and auto-restarts
 * those that exceed a consecutive failure threshold.
 */

import { emitEvent } from '@ava/core-v2/extensions'
import { createLogger } from '@ava/core-v2/logger'

const log = createLogger('MCPHealth')

export interface HealthCheckConfig {
  intervalMs: number // default 30000
  timeoutMs: number // default 10000
  maxFailures: number // default 3
}

export interface HealthStatus {
  serverId: string
  healthy: boolean
  lastCheck: number
  consecutiveFailures: number
  latencyMs?: number
}

/** Minimal interface for the manager methods the health monitor needs. */
export interface MCPManager {
  getConnectedServers(): string[]
  ping(serverId: string): Promise<void>
  restart(serverId: string): Promise<unknown>
}

export class MCPHealthMonitor {
  private statuses = new Map<string, HealthStatus>()
  private timer: ReturnType<typeof setInterval> | null = null
  private config: HealthCheckConfig

  constructor(
    private manager: MCPManager,
    config?: Partial<HealthCheckConfig>
  ) {
    this.config = {
      intervalMs: config?.intervalMs ?? 30_000,
      timeoutMs: config?.timeoutMs ?? 10_000,
      maxFailures: config?.maxFailures ?? 3,
    }
  }

  start(): void {
    if (this.timer) return
    const timer = setInterval(() => this.checkAll(), this.config.intervalMs)
    if (typeof timer === 'object' && 'unref' in timer) timer.unref()
    this.timer = timer
    log.debug('Health monitoring started')
  }
    this.timer = timer
    log.debug('Health monitoring started')
  }
    this.timer = timer
    log.debug('Health monitoring started')
  }
    this.timer = timer
    log.debug('Health monitoring started')
  }
    this.timer = timer
    log.debug('Health monitoring started')
  }
    this.timer = timer
    log.debug('Health monitoring started')
  }
    this.timer = timer
    log.debug('Health monitoring started')
  }

  stop(): void {
    if (this.timer) {
      clearInterval(this.timer)
      this.timer = null
    }
  }

  async checkAll(): Promise<Map<string, HealthStatus>> {
    const servers = this.manager.getConnectedServers()
    for (const serverId of servers) {
      await this.checkServer(serverId)
    }
    return new Map(this.statuses)
  }

  async checkServer(serverId: string): Promise<HealthStatus> {
    const start = Date.now()
    const existing = this.statuses.get(serverId) ?? {
      serverId,
      healthy: true,
      lastCheck: 0,
      consecutiveFailures: 0,
    }

    try {
      // Ping using a list tools request with timeout
      await Promise.race([
        this.manager.ping(serverId),
        new Promise<never>((_, reject) =>
          setTimeout(() => reject(new Error('Health check timeout')), this.config.timeoutMs)
        ),
      ])

      const status: HealthStatus = {
        serverId,
        healthy: true,
        lastCheck: Date.now(),
        consecutiveFailures: 0,
        latencyMs: Date.now() - start,
      }
      this.statuses.set(serverId, status)
      emitEvent('mcp:health', { serverId, healthy: true, latencyMs: status.latencyMs })
      return status
    } catch (err) {
      const failures = existing.consecutiveFailures + 1
      const status: HealthStatus = {
        serverId,
        healthy: false,
        lastCheck: Date.now(),
        consecutiveFailures: failures,
        latencyMs: Date.now() - start,
      }
      this.statuses.set(serverId, status)

      log.warn(
        `Health check failed for ${serverId}: ${err instanceof Error ? err.message : 'unknown'}`
      )
      emitEvent('mcp:health', {
        serverId,
        healthy: false,
        failures,
        error: err instanceof Error ? err.message : 'unknown',
      })

      // Auto-restart after maxFailures
      if (failures >= this.config.maxFailures) {
        log.warn(`Auto-restarting ${serverId} after ${failures} consecutive failures`)
        try {
          await this.manager.restart(serverId)
          status.consecutiveFailures = 0
          status.healthy = true
          emitEvent('mcp:restarted', { serverId })
        } catch {
          log.error(`Failed to restart ${serverId}`)
        }
      }

      return status
    }
  }

  getStatus(serverId: string): HealthStatus | undefined {
    return this.statuses.get(serverId)
  }

  getAllStatuses(): Map<string, HealthStatus> {
    return new Map(this.statuses)
  }
}
