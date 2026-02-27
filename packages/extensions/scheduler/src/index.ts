/**
 * Scheduler extension — background task scheduling.
 *
 * Manages periodic tasks like auto-save, indexing, and cleanup.
 * Other extensions register tasks via events.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createTaskRunner } from './runner.js'
import type { ScheduledTask, SchedulerConfig } from './types.js'
import { DEFAULT_SCHEDULER_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_SCHEDULER_CONFIG,
    ...api.getSettings<Partial<SchedulerConfig>>('scheduler'),
  }
  const runner = createTaskRunner(config)
  const disposables: Disposable[] = []
  let intervalId: ReturnType<typeof setInterval> | undefined

  // Listen for task registration
  disposables.push(
    api.on('scheduler:register', (data) => {
      const task = data as ScheduledTask
      runner.register(task)
      api.log.debug(`Scheduled task registered: ${task.name}`)
    })
  )

  // Listen for task unregistration
  disposables.push(
    api.on('scheduler:unregister', (data) => {
      const { taskId } = data as { taskId: string }
      runner.unregister(taskId)
      api.log.debug(`Scheduled task unregistered: ${taskId}`)
    })
  )

  // Start the tick loop
  intervalId = setInterval(() => {
    void runner.tick()
  }, config.tickInterval)

  api.log.debug('Scheduler extension activated')

  return {
    dispose() {
      if (intervalId !== undefined) clearInterval(intervalId)
      for (const d of disposables) d.dispose()
    },
  }
}
