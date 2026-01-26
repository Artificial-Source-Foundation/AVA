/**
 * Auto-Scaling Controller
 *
 * Manages dynamic resource scaling based on load and policies.
 */

import { randomUUID } from 'node:crypto'
import type {
  ScalingTarget,
  ScalingConfig,
  ScalingMetrics,
  ScalingDecision,
  ScalingPolicy,
  ScalingState,
  ScalingEvent,
  ScalingEventType,
  ScalableInstance,
  ScalingLimits,
  CapacityPlan,
} from './types.js'
import { scalingConfigSchema } from './types.js'

// =============================================================================
// Auto-Scaling Controller
// =============================================================================

export class AutoScalingController {
  private policies: Map<string, ScalingPolicy> = new Map()
  private states: Map<ScalingTarget, ScalingState> = new Map()
  private instances: Map<string, ScalableInstance> = new Map()
  private metricsHistory: Map<ScalingTarget, ScalingMetrics[]> = new Map()
  private eventHandlers: Array<(event: ScalingEvent) => void> = []
  private evaluationInterval?: ReturnType<typeof setInterval>
  private limits: ScalingLimits = {
    maxTotalInstances: 100,
    maxCostPerHour: 1000,
    maxScalingOperationsPerHour: 20,
    incidentRateLimit: true,
  }
  private scalingOperationsThisHour = 0
  private hourlyResetTimeout?: ReturnType<typeof setTimeout>

  // ===========================================================================
  // Configuration
  // ===========================================================================

  /**
   * Set scaling limits
   */
  setLimits(limits: Partial<ScalingLimits>): void {
    this.limits = { ...this.limits, ...limits }
  }

  /**
   * Add a scaling policy
   */
  addPolicy(policy: ScalingPolicy): void {
    // Validate config
    scalingConfigSchema.parse(policy.config)

    this.policies.set(policy.id, policy)

    // Initialize state if needed
    if (!this.states.has(policy.target)) {
      this.states.set(policy.target, {
        target: policy.target,
        currentInstances: policy.config.minInstances,
        desiredInstances: policy.config.minInstances,
        inCooldown: false,
        history: [],
      })
    }

    this.emitEvent('scaling.policy.added', policy.target, { policyId: policy.id })
  }

  /**
   * Remove a scaling policy
   */
  removePolicy(policyId: string): boolean {
    const policy = this.policies.get(policyId)
    if (!policy) return false

    this.policies.delete(policyId)
    this.emitEvent('scaling.policy.removed', policy.target, { policyId })

    return true
  }

  /**
   * Get a policy
   */
  getPolicy(policyId: string): ScalingPolicy | undefined {
    return this.policies.get(policyId)
  }

  /**
   * List policies
   */
  listPolicies(target?: ScalingTarget): ScalingPolicy[] {
    const policies = Array.from(this.policies.values())
    if (target) {
      return policies.filter((p) => p.target === target)
    }
    return policies
  }

  // ===========================================================================
  // Evaluation Loop
  // ===========================================================================

  /**
   * Start the auto-scaling evaluation loop
   */
  start(intervalMs = 10000): void {
    if (this.evaluationInterval) {
      this.stop()
    }

    this.evaluationInterval = setInterval(() => {
      this.evaluate()
    }, intervalMs)

    // Reset hourly counter
    this.hourlyResetTimeout = setTimeout(
      () => {
        this.scalingOperationsThisHour = 0
        this.scheduleHourlyReset()
      },
      3600000 - (Date.now() % 3600000)
    )

    // Run initial evaluation
    this.evaluate()
  }

  /**
   * Stop the auto-scaling evaluation loop
   */
  stop(): void {
    if (this.evaluationInterval) {
      clearInterval(this.evaluationInterval)
      this.evaluationInterval = undefined
    }

    if (this.hourlyResetTimeout) {
      clearTimeout(this.hourlyResetTimeout)
      this.hourlyResetTimeout = undefined
    }
  }

  private scheduleHourlyReset(): void {
    this.hourlyResetTimeout = setTimeout(() => {
      this.scalingOperationsThisHour = 0
      this.scheduleHourlyReset()
    }, 3600000)
  }

  /**
   * Run a single evaluation cycle
   */
  evaluate(): void {
    const targets: ScalingTarget[] = ['operators', 'oracles', 'validators', 'webhooks']

    for (const target of targets) {
      const policies = this.listPolicies(target).filter((p) => p.enabled)
      if (policies.length === 0) continue

      const state = this.states.get(target)
      if (!state) continue

      // Check cooldown
      if (state.inCooldown) {
        if (state.cooldownEndsAt && new Date(state.cooldownEndsAt) <= new Date()) {
          state.inCooldown = false
          state.cooldownEndsAt = undefined
        } else {
          continue
        }
      }

      // Get metrics
      const metrics = this.collectMetrics(target)
      this.recordMetrics(target, metrics)

      this.emitEvent('scaling.evaluation', target, { metrics })

      // Evaluate policies (sorted by priority)
      const sortedPolicies = [...policies].sort((a, b) => a.priority - b.priority)

      for (const policy of sortedPolicies) {
        const decision = this.evaluatePolicy(policy, metrics, state)

        if (decision.action !== 'no_action') {
          this.executeDecision(decision, state, policy.config)
          break // Only execute one action per cycle
        }
      }
    }
  }

  /**
   * Collect metrics for a target
   */
  private collectMetrics(target: ScalingTarget): ScalingMetrics {
    const instances = this.getInstances(target)
    const running = instances.filter((i) => i.status === 'running')

    // Calculate utilization
    let totalLoad = 0
    for (const instance of running) {
      totalLoad += instance.load
    }

    const currentUtilization = running.length > 0 ? totalLoad / running.length : 0

    // Calculate throughput and processing time from instances
    let totalTasks = 0
    let totalErrors = 0
    for (const instance of running) {
      totalTasks += instance.tasksProcessed
      totalErrors += instance.errors
    }

    return {
      currentInstances: running.length,
      targetUtilization: 0.7, // Default target
      currentUtilization,
      queueDepth: this.getQueueDepth(target),
      avgProcessingTime: this.getAvgProcessingTime(target),
      throughput: totalTasks,
      errorRate: totalTasks > 0 ? totalErrors / totalTasks : 0,
      timestamp: new Date().toISOString(),
    }
  }

  private getQueueDepth(_target: ScalingTarget): number {
    // In a real implementation, this would query the actual queue
    return 0
  }

  private getAvgProcessingTime(_target: ScalingTarget): number {
    // In a real implementation, this would calculate from metrics
    return 0
  }

  private recordMetrics(target: ScalingTarget, metrics: ScalingMetrics): void {
    const history = this.metricsHistory.get(target) || []
    history.push(metrics)

    // Keep last 100 metrics
    if (history.length > 100) {
      history.shift()
    }

    this.metricsHistory.set(target, history)
  }

  // ===========================================================================
  // Policy Evaluation
  // ===========================================================================

  /**
   * Evaluate a scaling policy
   */
  private evaluatePolicy(
    policy: ScalingPolicy,
    metrics: ScalingMetrics,
    state: ScalingState
  ): ScalingDecision {
    switch (policy.type) {
      case 'target_tracking':
        return this.evaluateTargetTracking(policy, metrics, state)
      case 'step_scaling':
        return this.evaluateStepScaling(policy, metrics, state)
      case 'scheduled':
        return this.evaluateScheduled(policy, state)
      case 'predictive':
        return this.evaluatePredictive(policy, metrics, state)
      default:
        return this.noAction(policy.target, metrics, state)
    }
  }

  /**
   * Target tracking policy
   */
  private evaluateTargetTracking(
    policy: ScalingPolicy,
    metrics: ScalingMetrics,
    state: ScalingState
  ): ScalingDecision {
    const { config, target } = policy
    const { currentUtilization } = metrics

    // Scale up if utilization exceeds threshold
    if (currentUtilization > config.scaleUpThreshold) {
      const desiredCount = Math.min(
        state.currentInstances + config.scaleUpStep,
        config.maxInstances
      )

      if (desiredCount > state.currentInstances) {
        return {
          target,
          action: 'scale_up',
          currentCount: state.currentInstances,
          desiredCount,
          reason: `Utilization ${(currentUtilization * 100).toFixed(1)}% exceeds threshold ${(config.scaleUpThreshold * 100).toFixed(1)}%`,
          metrics,
          timestamp: new Date().toISOString(),
        }
      }
    }

    // Scale down if utilization below threshold
    if (currentUtilization < config.scaleDownThreshold) {
      const desiredCount = Math.max(
        state.currentInstances - config.scaleDownStep,
        config.minInstances
      )

      if (desiredCount < state.currentInstances) {
        return {
          target,
          action: 'scale_down',
          currentCount: state.currentInstances,
          desiredCount,
          reason: `Utilization ${(currentUtilization * 100).toFixed(1)}% below threshold ${(config.scaleDownThreshold * 100).toFixed(1)}%`,
          metrics,
          timestamp: new Date().toISOString(),
        }
      }
    }

    return this.noAction(target, metrics, state)
  }

  /**
   * Step scaling policy
   */
  private evaluateStepScaling(
    policy: ScalingPolicy,
    metrics: ScalingMetrics,
    state: ScalingState
  ): ScalingDecision {
    const { config, target, steps } = policy
    if (!steps || steps.length === 0) {
      return this.noAction(target, metrics, state)
    }

    const { currentUtilization } = metrics

    // Find matching step
    for (const step of steps.sort((a, b) => b.threshold - a.threshold)) {
      let matches = false
      switch (step.operator) {
        case 'gt':
          matches = currentUtilization > step.threshold
          break
        case 'lt':
          matches = currentUtilization < step.threshold
          break
        case 'gte':
          matches = currentUtilization >= step.threshold
          break
        case 'lte':
          matches = currentUtilization <= step.threshold
          break
      }

      if (matches) {
        let desiredCount: number
        switch (step.adjustmentType) {
          case 'exact':
            desiredCount = step.adjustment
            break
          case 'change':
            desiredCount = state.currentInstances + step.adjustment
            break
          case 'percent':
            desiredCount = Math.round(state.currentInstances * (1 + step.adjustment / 100))
            break
        }

        desiredCount = Math.max(config.minInstances, Math.min(config.maxInstances, desiredCount))

        if (desiredCount !== state.currentInstances) {
          return {
            target,
            action: desiredCount > state.currentInstances ? 'scale_up' : 'scale_down',
            currentCount: state.currentInstances,
            desiredCount,
            reason: `Step scaling: utilization ${step.operator} ${step.threshold}`,
            metrics,
            timestamp: new Date().toISOString(),
          }
        }
      }
    }

    return this.noAction(target, metrics, state)
  }

  /**
   * Scheduled scaling policy
   */
  private evaluateScheduled(policy: ScalingPolicy, state: ScalingState): ScalingDecision {
    const { config, target, schedule } = policy
    if (!schedule || schedule.length === 0) {
      return this.noAction(
        target,
        { ...this.createEmptyMetrics(), currentInstances: state.currentInstances },
        state
      )
    }

    const now = new Date()

    for (const scheduled of schedule) {
      if (scheduled.at) {
        const scheduleTime = new Date(scheduled.at)
        // Check if we're within the scheduled window
        if (scheduled.duration) {
          const endTime = new Date(scheduleTime.getTime() + scheduled.duration * 1000)
          if (now >= scheduleTime && now <= endTime) {
            if (scheduled.desiredCount !== state.currentInstances) {
              return {
                target,
                action: scheduled.desiredCount > state.currentInstances ? 'scale_up' : 'scale_down',
                currentCount: state.currentInstances,
                desiredCount: Math.max(
                  config.minInstances,
                  Math.min(config.maxInstances, scheduled.desiredCount)
                ),
                reason: `Scheduled scaling at ${scheduled.at}`,
                metrics: { ...this.createEmptyMetrics(), currentInstances: state.currentInstances },
                timestamp: new Date().toISOString(),
              }
            }
          }
        }
      }

      // Cron support would require a cron parser library
    }

    return this.noAction(
      target,
      { ...this.createEmptyMetrics(), currentInstances: state.currentInstances },
      state
    )
  }

  /**
   * Predictive scaling policy
   */
  private evaluatePredictive(
    policy: ScalingPolicy,
    metrics: ScalingMetrics,
    state: ScalingState
  ): ScalingDecision {
    const { config, target, prediction } = policy
    if (!prediction) {
      return this.noAction(target, metrics, state)
    }

    // Get historical metrics
    const history = this.metricsHistory.get(target) || []
    if (history.length < 10) {
      // Not enough data for prediction
      return this.noAction(target, metrics, state)
    }

    // Simple prediction: use weighted average of recent trends
    const recentMetrics = history.slice(-10)
    const avgUtilization =
      recentMetrics.reduce((sum, m) => sum + m.currentUtilization, 0) / recentMetrics.length

    // Calculate trend
    const firstHalf = recentMetrics.slice(0, 5)
    const secondHalf = recentMetrics.slice(5)
    const firstAvg = firstHalf.reduce((sum, m) => sum + m.currentUtilization, 0) / 5
    const secondAvg = secondHalf.reduce((sum, m) => sum + m.currentUtilization, 0) / 5
    const trend = secondAvg - firstAvg

    // Predict future utilization
    const predictedUtilization = Math.max(0, Math.min(1, avgUtilization + trend * 2))

    // Calculate desired capacity
    if (predictedUtilization > config.scaleUpThreshold) {
      const desiredCount = Math.min(
        Math.ceil(state.currentInstances * (predictedUtilization / config.targetUtilization)),
        config.maxInstances
      )

      if (desiredCount > state.currentInstances) {
        return {
          target,
          action: 'scale_up',
          currentCount: state.currentInstances,
          desiredCount,
          reason: `Predictive: expected utilization ${(predictedUtilization * 100).toFixed(1)}%`,
          metrics,
          timestamp: new Date().toISOString(),
        }
      }
    }

    return this.noAction(target, metrics, state)
  }

  private noAction(
    target: ScalingTarget,
    metrics: ScalingMetrics,
    state: ScalingState
  ): ScalingDecision {
    return {
      target,
      action: 'no_action',
      currentCount: state.currentInstances,
      desiredCount: state.currentInstances,
      reason: 'Within acceptable thresholds',
      metrics,
      timestamp: new Date().toISOString(),
    }
  }

  private createEmptyMetrics(): ScalingMetrics {
    return {
      currentInstances: 0,
      targetUtilization: 0.7,
      currentUtilization: 0,
      queueDepth: 0,
      avgProcessingTime: 0,
      throughput: 0,
      errorRate: 0,
      timestamp: new Date().toISOString(),
    }
  }

  // ===========================================================================
  // Decision Execution
  // ===========================================================================

  /**
   * Execute a scaling decision
   */
  private async executeDecision(
    decision: ScalingDecision,
    state: ScalingState,
    config: ScalingConfig
  ): Promise<void> {
    // Check limits
    if (this.scalingOperationsThisHour >= this.limits.maxScalingOperationsPerHour) {
      this.emitEvent('scaling.failed', decision.target, {
        reason: 'Hourly operation limit reached',
        decision,
      })
      return
    }

    // Check total instances limit
    const totalInstances = Array.from(this.states.values()).reduce(
      (sum, s) => sum + s.currentInstances,
      0
    )

    if (decision.action === 'scale_up' && totalInstances >= this.limits.maxTotalInstances) {
      this.emitEvent('scaling.failed', decision.target, {
        reason: 'Total instance limit reached',
        decision,
      })
      return
    }

    this.emitEvent('scaling.decision', decision.target, { decision })
    this.emitEvent('scaling.started', decision.target, {
      action: decision.action,
      from: decision.currentCount,
      to: decision.desiredCount,
    })

    try {
      // Execute scaling
      if (decision.action === 'scale_up') {
        const toAdd = decision.desiredCount - decision.currentCount
        for (let i = 0; i < toAdd; i++) {
          await this.addInstance(decision.target)
        }
      } else if (decision.action === 'scale_down') {
        const toRemove = decision.currentCount - decision.desiredCount
        for (let i = 0; i < toRemove; i++) {
          await this.removeInstance(decision.target)
        }
      }

      // Update state
      state.currentInstances = decision.desiredCount
      state.desiredInstances = decision.desiredCount
      state.lastAction = decision
      state.lastScalingTime = new Date().toISOString()

      // Enter cooldown
      state.inCooldown = true
      state.cooldownEndsAt = new Date(Date.now() + config.cooldownSeconds * 1000).toISOString()

      // Record history
      state.history.push(decision)
      if (state.history.length > 50) {
        state.history.shift()
      }

      this.scalingOperationsThisHour++

      this.emitEvent('scaling.completed', decision.target, { decision })
      this.emitEvent('scaling.cooldown', decision.target, {
        endsAt: state.cooldownEndsAt,
      })
    } catch (error) {
      this.emitEvent('scaling.failed', decision.target, {
        reason: error instanceof Error ? error.message : String(error),
        decision,
      })
    }
  }

  // ===========================================================================
  // Instance Management
  // ===========================================================================

  /**
   * Add an instance
   */
  async addInstance(target: ScalingTarget): Promise<ScalableInstance> {
    const instance: ScalableInstance = {
      id: `${target}_${randomUUID().slice(0, 8)}`,
      type: target,
      status: 'starting',
      startedAt: new Date().toISOString(),
      load: 0,
      tasksProcessed: 0,
      errors: 0,
      metadata: {},
    }

    this.instances.set(instance.id, instance)

    // Simulate startup time
    setTimeout(() => {
      instance.status = 'running'
    }, 1000)

    return instance
  }

  /**
   * Remove an instance
   */
  async removeInstance(target: ScalingTarget): Promise<boolean> {
    // Find lowest load instance of target type
    const targetInstances = this.getInstances(target).filter((i) => i.status === 'running')
    if (targetInstances.length === 0) return false

    const toRemove = targetInstances.sort((a, b) => a.load - b.load)[0]
    toRemove.status = 'stopping'

    // Simulate graceful shutdown
    setTimeout(() => {
      toRemove.status = 'stopped'
      this.instances.delete(toRemove.id)
    }, 1000)

    return true
  }

  /**
   * Get instances for a target
   */
  getInstances(target: ScalingTarget): ScalableInstance[] {
    return Array.from(this.instances.values()).filter((i) => i.type === target)
  }

  /**
   * Update instance metrics
   */
  updateInstanceMetrics(
    instanceId: string,
    metrics: Partial<Pick<ScalableInstance, 'load' | 'tasksProcessed' | 'errors'>>
  ): void {
    const instance = this.instances.get(instanceId)
    if (instance) {
      Object.assign(instance, metrics)
    }
  }

  // ===========================================================================
  // Capacity Planning
  // ===========================================================================

  /**
   * Generate capacity plan
   */
  generateCapacityPlan(target: ScalingTarget): CapacityPlan {
    const state = this.states.get(target)
    const history = this.metricsHistory.get(target) || []
    const policies = this.listPolicies(target)

    const currentCapacity = state?.currentInstances || 0

    // Analyze historical data
    let peakUtilization = 0
    let avgUtilization = 0

    if (history.length > 0) {
      peakUtilization = Math.max(...history.map((m) => m.currentUtilization))
      avgUtilization = history.reduce((sum, m) => sum + m.currentUtilization, 0) / history.length
    }

    // Calculate recommended capacity
    const targetUtilization = policies[0]?.config.targetUtilization || 0.7
    const recommendedForPeak = Math.ceil((currentCapacity * peakUtilization) / targetUtilization)
    const recommendedForAvg = Math.ceil((currentCapacity * avgUtilization) / targetUtilization)

    // Use a blend of peak and average
    const recommendedCapacity = Math.ceil(recommendedForAvg * 0.7 + recommendedForPeak * 0.3)

    // Estimate costs (simplified)
    const costPerInstance = 0.1 // $ per hour
    const currentCost = currentCapacity * costPerInstance
    const recommendedCost = recommendedCapacity * costPerInstance

    return {
      target,
      currentCapacity,
      recommendedCapacity: Math.max(1, recommendedCapacity),
      peakLoad: peakUtilization,
      confidence: history.length >= 20 ? 0.8 : history.length / 25,
      timeHorizon: '1 hour',
      reasoning: `Based on ${history.length} data points, peak utilization of ${(peakUtilization * 100).toFixed(1)}%`,
      costImpact: {
        current: currentCost,
        recommended: recommendedCost,
        savings: currentCost - recommendedCost,
      },
    }
  }

  // ===========================================================================
  // Event System
  // ===========================================================================

  private emitEvent(
    type: ScalingEventType,
    target: ScalingTarget,
    data: Record<string, unknown>
  ): void {
    const event: ScalingEvent = {
      type,
      timestamp: new Date().toISOString(),
      target,
      data,
    }

    for (const handler of this.eventHandlers) {
      try {
        handler(event)
      } catch {
        // Ignore handler errors
      }
    }
  }

  onEvent(handler: (event: ScalingEvent) => void): () => void {
    this.eventHandlers.push(handler)
    return () => {
      const index = this.eventHandlers.indexOf(handler)
      if (index >= 0) {
        this.eventHandlers.splice(index, 1)
      }
    }
  }

  // ===========================================================================
  // Getters
  // ===========================================================================

  /**
   * Get scaling state
   */
  getState(target: ScalingTarget): ScalingState | undefined {
    return this.states.get(target)
  }

  /**
   * Get all states
   */
  getAllStates(): ScalingState[] {
    return Array.from(this.states.values())
  }

  /**
   * Get metrics history
   */
  getMetricsHistory(target: ScalingTarget, limit = 50): ScalingMetrics[] {
    const history = this.metricsHistory.get(target) || []
    return history.slice(-limit)
  }

  /**
   * Get stats
   */
  getStats(): {
    totalInstances: number
    instancesByTarget: Record<ScalingTarget, number>
    policiesCount: number
    scalingOperationsThisHour: number
    activeAlerts: number
  } {
    const instancesByTarget: Record<ScalingTarget, number> = {
      operators: 0,
      oracles: 0,
      validators: 0,
      webhooks: 0,
    }

    for (const instance of this.instances.values()) {
      if (instance.status === 'running') {
        instancesByTarget[instance.type]++
      }
    }

    return {
      totalInstances: Array.from(this.instances.values()).filter((i) => i.status === 'running')
        .length,
      instancesByTarget,
      policiesCount: this.policies.size,
      scalingOperationsThisHour: this.scalingOperationsThisHour,
      activeAlerts: 0,
    }
  }
}
