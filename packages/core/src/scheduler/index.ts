/**
 * @ava/core Scheduler Module
 * Background task scheduling for cleanup and maintenance
 */

// Scheduler
export {
  createScheduler,
  disposeScheduler,
  getScheduler,
  Scheduler,
  setScheduler,
} from './scheduler.js'
// Types
export type { ScheduledTask, SchedulerConfig, TaskResult, TaskScope } from './types.js'
