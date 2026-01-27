/**
 * Delta9 Squadron Manager
 *
 * Orchestrates wave-based batch agent execution.
 * Builds on SubagentManager for individual agent tracking.
 *
 * Features:
 * - Parallel agent execution within waves
 * - Sequential wave advancement
 * - Auto-advance when waves complete
 * - Event callbacks for toast notifications
 */

import { nanoid } from 'nanoid'
import { uniqueNamesGenerator, adjectives, starWars } from 'unique-names-generator'
import type { MissionState } from '../mission/state.js'
import { getSubagentManager } from '../subagents/manager.js'
import type { OpenCodeClient } from '../lib/background-manager.js'
import { getNamedLogger } from '../lib/logger.js'
import { showSquadronToast, squadronNotifications, type ToastClient } from '../lib/notifications.js'
import type {
  Squadron,
  SquadronConfig,
  SquadronStatus,
  SpawnSquadronInput,
  SquadronResult,
  Wave,
  WaveAgent,
  WaveResult,
  WaveStatus,
  SquadronEvent,
  SquadronEventType,
} from './types.js'
import { DEFAULT_SQUADRON_CONFIG } from './types.js'

const log = getNamedLogger('squadrons')

// =============================================================================
// Constants
// =============================================================================

const POLL_INTERVAL_MS = 1000 // Check wave status every second

// =============================================================================
// Event Handler Type
// =============================================================================

export type SquadronEventHandler = (event: SquadronEvent) => void | Promise<void>

// =============================================================================
// Squadron Manager
// =============================================================================

export class SquadronManager {
  private squadrons = new Map<string, Squadron>()
  private aliasToId = new Map<string, string>()
  private pollingIntervals = new Map<string, ReturnType<typeof setInterval>>()
  private eventHandlers = new Set<SquadronEventHandler>()

  private readonly missionState: MissionState
  private readonly cwd: string
  private readonly client: OpenCodeClient | undefined
  private readonly config: SquadronConfig

  constructor(
    missionState: MissionState,
    cwd: string,
    client?: OpenCodeClient,
    config?: Partial<SquadronConfig>
  ) {
    this.missionState = missionState
    this.cwd = cwd
    this.client = client
    this.config = { ...DEFAULT_SQUADRON_CONFIG, ...config }
  }

  // ===========================================================================
  // Event Handling
  // ===========================================================================

  /**
   * Register event handler for squadron events
   */
  onEvent(handler: SquadronEventHandler): () => void {
    this.eventHandlers.add(handler)
    return () => this.eventHandlers.delete(handler)
  }

  /**
   * Emit squadron event to all handlers
   */
  private emitEvent(
    type: SquadronEventType,
    squadron: Squadron,
    waveNumber?: number,
    agentId?: string,
    agentAlias?: string,
    data?: Record<string, unknown>
  ): void {
    const event: SquadronEvent = {
      type,
      squadronId: squadron.id,
      squadronAlias: squadron.alias,
      waveNumber,
      agentId,
      agentAlias,
      timestamp: new Date().toISOString(),
      data,
    }

    for (const handler of this.eventHandlers) {
      try {
        handler(event)
      } catch (error) {
        log.error(`Event handler error: ${error instanceof Error ? error.message : String(error)}`)
      }
    }
  }

  // ===========================================================================
  // Alias Generation
  // ===========================================================================

  /**
   * Generate a unique squadron alias like "alpha-strike"
   */
  private generateAlias(): string {
    let alias: string
    let attempts = 0
    const maxAttempts = 10

    do {
      alias = uniqueNamesGenerator({
        dictionaries: [adjectives, starWars],
        separator: '-',
        length: 2,
        style: 'lowerCase',
      })
      attempts++
    } while (this.aliasToId.has(alias) && attempts < maxAttempts)

    if (attempts >= maxAttempts) {
      alias = `${alias}-${Date.now()}`
    }

    return alias
  }

  // ===========================================================================
  // Core Operations
  // ===========================================================================

  /**
   * Spawn a new squadron with waves of agents
   */
  async spawnSquadron(input: SpawnSquadronInput): Promise<Squadron> {
    const alias = input.alias || this.generateAlias()

    if (this.aliasToId.has(alias)) {
      throw new Error(`Squadron alias already in use: ${alias}`)
    }

    // Create squadron
    const squadronId = `sqd_${nanoid(8)}`
    const config = { ...this.config, ...input.config }

    // Build waves from input
    const waves: Wave[] = input.waves.map((waveInput, index) => {
      const waveId = `wave_${nanoid(6)}`
      const agents: WaveAgent[] = waveInput.agents.map((agentInput) => ({
        id: `agent_${nanoid(6)}`,
        agentType: agentInput.type,
        prompt: agentInput.prompt,
        context: agentInput.context,
        skills: agentInput.skills,
        state: 'pending' as const,
      }))

      return {
        id: waveId,
        number: index + 1,
        agents,
        status: 'pending' as WaveStatus,
      }
    })

    const squadron: Squadron = {
      id: squadronId,
      alias,
      description: input.description,
      waves,
      currentWave: 0,
      status: 'pending',
      parentSessionId: input.parentSessionId,
      config,
      createdAt: new Date().toISOString(),
    }

    this.squadrons.set(squadronId, squadron)
    this.aliasToId.set(alias, squadronId)

    log.info(`Squadron ${alias} created with ${waves.length} waves`)

    // Start first wave
    await this.startWave(squadron, 1)

    return squadron
  }

  /**
   * Start a specific wave within a squadron
   */
  private async startWave(squadron: Squadron, waveNumber: number): Promise<void> {
    const wave = squadron.waves.find((w) => w.number === waveNumber)
    if (!wave) {
      throw new Error(`Wave ${waveNumber} not found in squadron ${squadron.alias}`)
    }

    squadron.currentWave = waveNumber
    squadron.status = 'running'
    wave.status = 'running'
    wave.startedAt = new Date().toISOString()

    // Set timeout
    const config = squadron.config || this.config
    const timeout = config.waveTimeout || this.config.waveTimeout
    wave.timeoutAt = new Date(Date.now() + timeout).toISOString()

    this.emitEvent('wave_started', squadron, waveNumber, undefined, undefined, {
      agentCount: wave.agents.length,
    })

    log.info(
      `Squadron ${squadron.alias}: Starting wave ${waveNumber} with ${wave.agents.length} agents`
    )

    // Spawn all agents in this wave in parallel
    const subagentManager = getSubagentManager(this.missionState, this.cwd, this.client)

    const spawnPromises = wave.agents.map(async (agent) => {
      try {
        agent.state = 'spawning'
        agent.spawnedAt = new Date().toISOString()

        const subagent = await subagentManager.spawn({
          prompt: agent.prompt,
          agentType: agent.agentType,
          context: agent.context,
          skills: agent.skills,
          parentSessionId: squadron.parentSessionId,
        })

        agent.subagentId = subagent.taskId
        agent.alias = subagent.alias
        agent.state = 'active'

        this.emitEvent('agent_started', squadron, waveNumber, agent.id, agent.alias)

        log.debug(`Squadron ${squadron.alias}: Agent ${agent.alias} spawned`)
      } catch (error) {
        agent.state = 'failed'
        agent.error = error instanceof Error ? error.message : String(error)
        agent.completedAt = new Date().toISOString()

        this.emitEvent('agent_failed', squadron, waveNumber, agent.id, agent.alias, {
          error: agent.error,
        })

        log.error(`Squadron ${squadron.alias}: Agent spawn failed: ${agent.error}`)
      }
    })

    await Promise.all(spawnPromises)

    // Start polling for wave completion
    this.startWavePolling(squadron.id)
  }

  /**
   * Start polling for wave completion
   */
  private startWavePolling(squadronId: string): void {
    if (this.pollingIntervals.has(squadronId)) return

    const interval = setInterval(() => {
      this.checkWaveCompletion(squadronId)
    }, POLL_INTERVAL_MS)

    interval.unref()
    this.pollingIntervals.set(squadronId, interval)
  }

  /**
   * Stop polling for a squadron
   */
  private stopWavePolling(squadronId: string): void {
    const interval = this.pollingIntervals.get(squadronId)
    if (interval) {
      clearInterval(interval)
      this.pollingIntervals.delete(squadronId)
    }
  }

  /**
   * Check if current wave is complete
   */
  private async checkWaveCompletion(squadronId: string): Promise<void> {
    const squadron = this.squadrons.get(squadronId)
    if (!squadron || squadron.status !== 'running') {
      this.stopWavePolling(squadronId)
      return
    }

    const wave = squadron.waves.find((w) => w.number === squadron.currentWave)
    if (!wave || wave.status !== 'running') return

    const subagentManager = getSubagentManager(this.missionState, this.cwd, this.client)

    // Update agent states from subagent manager
    let allDone = true
    let anyFailed = false

    for (const agent of wave.agents) {
      // Skip already completed/failed agents
      const currentState = agent.state as string
      if (currentState === 'completed' || currentState === 'failed') continue

      if (!agent.subagentId) {
        // Not yet spawned - mark as not done
        allDone = false
        continue
      }

      const subagent = subagentManager.getByTaskId(agent.subagentId)
      if (!subagent) {
        allDone = false
        continue
      }

      const previousState = agent.state as string

      if (subagent.state === 'completed') {
        agent.state = 'completed'
        agent.output = subagent.output
        agent.completedAt = new Date().toISOString()

        if (previousState !== 'completed') {
          this.emitEvent('agent_completed', squadron, wave.number, agent.id, agent.alias, {
            hasOutput: !!agent.output,
          })
          log.debug(`Squadron ${squadron.alias}: Agent ${agent.alias} completed`)
        }
      } else if (subagent.state === 'failed') {
        agent.state = 'failed'
        agent.error = subagent.error
        agent.completedAt = new Date().toISOString()
        anyFailed = true

        if (previousState !== 'failed') {
          this.emitEvent('agent_failed', squadron, wave.number, agent.id, agent.alias, {
            error: agent.error,
          })
          log.debug(`Squadron ${squadron.alias}: Agent ${agent.alias} failed`)
        }
      } else {
        agent.state = subagent.state as WaveAgent['state']
        allDone = false
      }
    }

    // Check timeout
    if (wave.timeoutAt && new Date() > new Date(wave.timeoutAt)) {
      wave.status = 'failed'
      wave.completedAt = new Date().toISOString()
      squadron.status = 'failed'
      squadron.completedAt = new Date().toISOString()

      this.emitEvent('wave_failed', squadron, wave.number, undefined, undefined, {
        reason: 'timeout',
      })
      this.emitEvent('squadron_failed', squadron, undefined, undefined, undefined, {
        reason: 'wave_timeout',
        waveNumber: wave.number,
      })

      this.stopWavePolling(squadronId)
      log.error(`Squadron ${squadron.alias}: Wave ${wave.number} timed out`)
      return
    }

    // All agents done
    if (allDone) {
      wave.completedAt = new Date().toISOString()

      if (anyFailed) {
        wave.status = 'failed'
        this.emitEvent('wave_failed', squadron, wave.number, undefined, undefined, {
          reason: 'agent_failures',
        })
        log.warn(`Squadron ${squadron.alias}: Wave ${wave.number} completed with failures`)
      } else {
        wave.status = 'completed'
        this.emitEvent('wave_completed', squadron, wave.number, undefined, undefined, {
          agentCount: wave.agents.length,
        })
        log.info(`Squadron ${squadron.alias}: Wave ${wave.number} completed successfully`)
      }

      // Check if there are more waves
      const nextWaveNumber = squadron.currentWave + 1
      const nextWave = squadron.waves.find((w) => w.number === nextWaveNumber)

      if (nextWave) {
        const config = squadron.config || this.config
        if (config.autoAdvance !== false && !anyFailed) {
          // Start next wave
          await this.startWave(squadron, nextWaveNumber)
        } else {
          // Wait for manual advancement or squadron is failed
          this.stopWavePolling(squadronId)
          if (anyFailed) {
            squadron.status = 'failed'
            squadron.completedAt = new Date().toISOString()
            this.emitEvent('squadron_failed', squadron, undefined, undefined, undefined, {
              reason: 'wave_failure',
              waveNumber: wave.number,
            })
          }
        }
      } else {
        // All waves complete
        this.stopWavePolling(squadronId)
        squadron.status = anyFailed ? 'failed' : 'completed'
        squadron.completedAt = new Date().toISOString()

        if (anyFailed) {
          this.emitEvent('squadron_failed', squadron, undefined, undefined, undefined, {
            reason: 'agent_failures',
          })
          log.warn(`Squadron ${squadron.alias}: Completed with failures`)
        } else {
          this.emitEvent('squadron_completed', squadron)
          log.info(`Squadron ${squadron.alias}: All waves completed successfully`)
        }
      }
    }
  }

  /**
   * Manually advance to next wave
   */
  async advanceWave(squadronId: string): Promise<Wave | null> {
    const squadron = this.squadrons.get(squadronId)
    if (!squadron) return null

    const currentWave = squadron.waves.find((w) => w.number === squadron.currentWave)
    if (!currentWave || currentWave.status === 'running') {
      throw new Error('Cannot advance: current wave is still running')
    }

    const nextWaveNumber = squadron.currentWave + 1
    const nextWave = squadron.waves.find((w) => w.number === nextWaveNumber)

    if (!nextWave) {
      return null // No more waves
    }

    await this.startWave(squadron, nextWaveNumber)
    return nextWave
  }

  /**
   * Wait for a specific wave to complete
   */
  async waitForWave(
    squadronId: string,
    waveNumber: number,
    timeoutMs?: number
  ): Promise<WaveResult> {
    const squadron = this.squadrons.get(squadronId)
    if (!squadron) {
      throw new Error(`Squadron not found: ${squadronId}`)
    }

    const wave = squadron.waves.find((w) => w.number === waveNumber)
    if (!wave) {
      throw new Error(`Wave ${waveNumber} not found in squadron ${squadron.alias}`)
    }

    const config = squadron.config || this.config
    const timeout = timeoutMs || config.waveTimeout || this.config.waveTimeout
    const startTime = Date.now()

    while (Date.now() - startTime < timeout) {
      if (wave.status === 'completed' || wave.status === 'failed') {
        return this.buildWaveResult(wave)
      }

      await new Promise((resolve) => setTimeout(resolve, POLL_INTERVAL_MS))
    }

    throw new Error(`Wait for wave ${waveNumber} timed out`)
  }

  /**
   * Wait for entire squadron to complete
   */
  async waitForSquadron(squadronId: string, timeoutMs?: number): Promise<SquadronResult> {
    const squadron = this.squadrons.get(squadronId)
    if (!squadron) {
      throw new Error(`Squadron not found: ${squadronId}`)
    }

    const waveTimeout = squadron.config?.waveTimeout ?? this.config.waveTimeout
    const timeout = timeoutMs ?? waveTimeout * squadron.waves.length
    const startTime = Date.now()

    while (Date.now() - startTime < timeout) {
      if (
        squadron.status === 'completed' ||
        squadron.status === 'failed' ||
        squadron.status === 'cancelled'
      ) {
        return this.buildSquadronResult(squadron)
      }

      await new Promise((resolve) => setTimeout(resolve, POLL_INTERVAL_MS))
    }

    throw new Error(`Wait for squadron ${squadron.alias} timed out`)
  }

  /**
   * Cancel a squadron
   */
  cancelSquadron(squadronId: string): boolean {
    const squadron = this.squadrons.get(squadronId)
    if (!squadron) return false

    if (squadron.status !== 'running') return false

    squadron.status = 'cancelled'
    squadron.completedAt = new Date().toISOString()

    const currentWave = squadron.waves.find((w) => w.number === squadron.currentWave)
    if (currentWave && currentWave.status === 'running') {
      currentWave.status = 'failed'
      currentWave.completedAt = new Date().toISOString()
    }

    this.stopWavePolling(squadronId)
    this.emitEvent('squadron_cancelled', squadron)

    log.info(`Squadron ${squadron.alias}: Cancelled`)
    return true
  }

  // ===========================================================================
  // Query Operations
  // ===========================================================================

  /**
   * Get squadron by ID
   */
  getSquadron(id: string): Squadron | null {
    return this.squadrons.get(id) || null
  }

  /**
   * Get squadron by alias
   */
  getByAlias(alias: string): Squadron | null {
    const id = this.aliasToId.get(alias)
    if (!id) return null
    return this.squadrons.get(id) || null
  }

  /**
   * List squadrons with optional filters
   */
  listSquadrons(filter?: { status?: SquadronStatus; parentSessionId?: string }): Squadron[] {
    let results = Array.from(this.squadrons.values())

    if (filter?.status) {
      results = results.filter((s) => s.status === filter.status)
    }

    if (filter?.parentSessionId) {
      results = results.filter((s) => s.parentSessionId === filter.parentSessionId)
    }

    // Sort by creation time (newest first)
    return results.sort((a, b) => new Date(b.createdAt).getTime() - new Date(a.createdAt).getTime())
  }

  // ===========================================================================
  // Result Building
  // ===========================================================================

  private buildWaveResult(wave: Wave): WaveResult {
    return {
      number: wave.number,
      status: wave.status,
      agents: wave.agents.map((agent) => ({
        agentType: agent.agentType,
        alias: agent.alias,
        state: agent.state,
        output: agent.output,
        error: agent.error,
        duration:
          agent.spawnedAt && agent.completedAt
            ? new Date(agent.completedAt).getTime() - new Date(agent.spawnedAt).getTime()
            : undefined,
      })),
      duration:
        wave.startedAt && wave.completedAt
          ? new Date(wave.completedAt).getTime() - new Date(wave.startedAt).getTime()
          : undefined,
    }
  }

  private buildSquadronResult(squadron: Squadron): SquadronResult {
    return {
      id: squadron.id,
      alias: squadron.alias,
      status: squadron.status,
      waves: squadron.waves.map((wave) => this.buildWaveResult(wave)),
      duration:
        squadron.createdAt && squadron.completedAt
          ? new Date(squadron.completedAt).getTime() - new Date(squadron.createdAt).getTime()
          : undefined,
    }
  }

  // ===========================================================================
  // Toast Integration
  // ===========================================================================

  /**
   * Enable toast notifications for squadron events
   *
   * Call this with the SDK client to show visual notifications
   * when squadrons start, complete, or fail.
   */
  enableToasts(client: ToastClient): () => void {
    return this.onEvent(async (event) => {
      const squadron = this.squadrons.get(event.squadronId)
      if (!squadron) return

      switch (event.type) {
        case 'squadron_started':
          // Internal notification (no toast spam for start)
          squadronNotifications.started(
            event.squadronAlias,
            squadron.waves.length,
            squadron.waves.reduce((sum, w) => sum + w.agents.length, 0)
          )
          break

        case 'wave_started': {
          const wave = squadron.waves.find((w) => w.number === event.waveNumber)
          if (wave) {
            await showSquadronToast(client, {
              squadronAlias: event.squadronAlias,
              title: `Squadron ${event.squadronAlias}: Wave ${event.waveNumber} Starting`,
              agents: wave.agents.map((a) => ({
                name: a.alias || a.agentType,
                status: 'starting',
              })),
              variant: 'info',
            })
          }
          break
        }

        case 'wave_completed':
          await showSquadronToast(client, {
            squadronAlias: event.squadronAlias,
            title: `Squadron ${event.squadronAlias}: Wave ${event.waveNumber} Complete`,
            agents: [],
            variant: 'success',
          })
          break

        case 'wave_failed':
          await showSquadronToast(client, {
            squadronAlias: event.squadronAlias,
            title: `Squadron ${event.squadronAlias}: Wave ${event.waveNumber} Failed`,
            agents: [],
            variant: 'error',
          })
          break

        case 'squadron_completed':
          await showSquadronToast(client, {
            squadronAlias: event.squadronAlias,
            title: `Squadron ${event.squadronAlias}: All Waves Complete!`,
            agents: squadron.waves.flatMap((w) =>
              w.agents.map((a) => ({
                name: a.alias || a.agentType,
                status: a.state,
              }))
            ),
            variant: 'success',
          })
          break

        case 'squadron_failed':
          await showSquadronToast(client, {
            squadronAlias: event.squadronAlias,
            title: `Squadron ${event.squadronAlias}: Failed`,
            agents: [],
            variant: 'error',
          })
          break

        case 'squadron_cancelled':
          squadronNotifications.cancelled(event.squadronAlias)
          break
      }
    })
  }

  // ===========================================================================
  // Cleanup
  // ===========================================================================

  /**
   * Clean up completed squadrons
   */
  cleanup(): number {
    let cleaned = 0

    for (const [id, squadron] of this.squadrons) {
      if (
        squadron.status === 'completed' ||
        squadron.status === 'failed' ||
        squadron.status === 'cancelled'
      ) {
        this.squadrons.delete(id)
        this.aliasToId.delete(squadron.alias)
        cleaned++
      }
    }

    return cleaned
  }

  /**
   * Shutdown manager
   */
  shutdown(): void {
    for (const squadronId of this.pollingIntervals.keys()) {
      this.stopWavePolling(squadronId)
    }
    this.squadrons.clear()
    this.aliasToId.clear()
    this.eventHandlers.clear()
  }
}

// =============================================================================
// Singleton
// =============================================================================

let globalSquadronManager: SquadronManager | null = null

export function getSquadronManager(
  missionState: MissionState,
  cwd: string,
  client?: OpenCodeClient,
  config?: Partial<SquadronConfig>
): SquadronManager {
  if (!globalSquadronManager) {
    globalSquadronManager = new SquadronManager(missionState, cwd, client, config)
  }
  return globalSquadronManager
}

export function resetSquadronManager(): void {
  if (globalSquadronManager) {
    globalSquadronManager.shutdown()
    globalSquadronManager = null
  }
}
