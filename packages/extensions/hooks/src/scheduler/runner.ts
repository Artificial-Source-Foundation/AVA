/**
 * Task runner — executes scheduled tasks respecting concurrency limits.
 */

import type { ScheduledTask, SchedulerConfig } from './types.js'
import { DEFAULT_SCHEDULER_CONFIG } from './types.js'

export interface TaskRunner {
  register(task: ScheduledTask): void
  unregister(taskId: string): void
  tick(): Promise<void>
  getTasks(): ReadonlyMap<string, ScheduledTask>
  getRunningCount(): number
}

export function createTaskRunner(config: SchedulerConfig = DEFAULT_SCHEDULER_CONFIG): TaskRunner {
  const tasks = new Map<string, ScheduledTask>()
  let runningCount = 0

  return {
    register(task: ScheduledTask): void {
      tasks.set(task.id, task)
    },

    unregister(taskId: string): void {
      tasks.delete(taskId)
    },

    async tick(): Promise<void> {
      const now = Date.now()
      const due: ScheduledTask[] = []

      for (const task of tasks.values()) {
        if (task.nextRun <= now) due.push(task)
      }

      // Sort by nextRun (earliest first)
      due.sort((a, b) => a.nextRun - b.nextRun)

      const available = config.maxConcurrent - runningCount
      const toRun = due.slice(0, Math.max(0, available))

      const promises = toRun.map(async (task) => {
        runningCount++
        try {
          await task.handler()
          task.lastRun = Date.now()
          task.nextRun = task.lastRun + task.interval
        } finally {
          runningCount--
        }
      })

      await Promise.allSettled(promises)
    },

    getTasks(): ReadonlyMap<string, ScheduledTask> {
      return tasks
    },

    getRunningCount(): number {
      return runningCount
    },
  }
}
