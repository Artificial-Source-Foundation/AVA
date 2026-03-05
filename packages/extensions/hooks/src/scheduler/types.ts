/**
 * Scheduler types.
 */

export interface ScheduledTask {
  id: string
  name: string
  interval: number
  lastRun?: number
  nextRun: number
  handler: () => Promise<void>
}

export interface SchedulerConfig {
  maxConcurrent: number
  tickInterval: number
}

export const DEFAULT_SCHEDULER_CONFIG: SchedulerConfig = {
  maxConcurrent: 3,
  tickInterval: 1000,
}
