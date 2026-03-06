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
    if (typeof timer === 'object' && 'unref' in timer) {
      ;(timer as ReturnType<typeof setInterval> & { unref(): void }).unref()
    }
    this.timer = timer
    log.debug('Health monitoring started')
  }

  stop(): void {
    if (this.timer) {
      clearInterval(this.timer)
      this.timer = null
      log.debug('Health monitoring stopped')
    }
  }

  getStatus(serverId: string): HealthStatus | undefined {
    return this.statuses.get(serverId)
  }

  getAllStatuses(): HealthStatus[] {
    return Array.from(this.statuses.values())
  }

  private async checkAll(): Promise<void> {
    const servers = this.manager.getConnectedServers()
    const now = Date.now()

    for (const serverId of servers) {
      await this.checkOne(serverId, now)
    }
  }

  private async checkOne(serverId: string, now: number): Promise<void> {
    const start = now

    try {
      await Promise.race([
        this.manager.ping(serverId),
        new Promise<void>((_, reject) =>
          setTimeout(() => reject(new Error('Ping timeout')), this.config.timeoutMs)
        ),
      ])

      const latency = Date.now() - start
      this.updateStatus(serverId, {
        healthy: true,
        lastCheck: now,
        consecutiveFailures: 0,
        latencyMs: latency,
      })
    } catch (error) {
      const current = this.statuses.get(serverId)
      const failures = (current?.consecutiveFailures ?? 0) + 1

      this.updateStatus(serverId, {
        healthy: false,
        lastCheck: now,
        consecutiveFailures: failures,
        latencyMs: undefined,
      })

      if (failures >= this.config.maxFailures) {
        await this.handleUnhealthy(serverId, failures)
      }
    }
  }

  private updateStatus(serverId: string, update: Partial<HealthStatus>): void {
    const current = this.statuses.get(serverId)
    const status: HealthStatus = {
      serverId,
      healthy: update.healthy ?? current?.healthy ?? false,
      lastCheck: update.lastCheck ?? current?.lastCheck ?? 0,
      consecutiveFailures: update.consecutiveFailures ?? current?.consecutiveFailures ?? 0,
      latencyMs: update.latencyMs ?? current?.latencyMs,
    }
    this.statuses.set(serverId, status)
  }

  private async handleUnhealthy(serverId: string, failures: number): Promise<void> {
    log.warn(
      `MCP server ${serverId} marked unhealthy after ${failures} consecutive failures. Restarting...`
    )

    emitEvent('mcp:health:unhealthy', { serverId, failures })

    try {
      await this.manager.restart(serverId)
      log.info(`MCP server ${serverId} restarted successfully`)

      // Reset failure count after successful restart
      const status = this.statuses.get(serverId)
      if (status) {
        status.consecutiveFailures = 0
        status.healthy = true
      }

      emitEvent('mcp:health:recovered', { serverId })
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      log.error(`Failed to restart MCP server ${serverId}: ${message}`)

      emitEvent('mcp:health:restart-failed', { serverId, error: message })
    }
  }
}
