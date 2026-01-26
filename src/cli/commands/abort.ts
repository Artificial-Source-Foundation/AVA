/**
 * Delta9 Abort Command
 *
 * Abort the current mission:
 * - Marks mission as aborted
 * - Cancels pending/in-progress tasks
 * - Creates checkpoint for recovery
 */

import { existsSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { join } from 'node:path'
import type { AbortOptions, AbortResult } from '../types.js'
import { colorize, symbols } from '../types.js'

// =============================================================================
// Abort Command
// =============================================================================

export async function abortCommand(options: AbortOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'summary'

  // Execute abort
  const result = await executeAbort(cwd, {
    reason: options.reason,
    force: options.force,
    createCheckpoint: options.checkpoint !== false,
  })

  // Output based on format
  switch (format) {
    case 'json':
      console.log(JSON.stringify(result, null, 2))
      break
    case 'summary':
    default:
      printAbortResult(result)
      break
  }

  // Exit with error code if abort failed
  if (!result.success) {
    process.exit(1)
  }
}

// =============================================================================
// Abort Execution
// =============================================================================

interface AbortExecutionOptions {
  reason?: string
  force?: boolean
  createCheckpoint?: boolean
}

async function executeAbort(cwd: string, options: AbortExecutionOptions): Promise<AbortResult> {
  const missionFile = join(cwd, '.delta9', 'mission.json')

  // Check if mission exists
  if (!existsSync(missionFile)) {
    return {
      success: false,
      error: 'No active mission found',
      timestamp: new Date().toISOString(),
    }
  }

  try {
    // Load mission
    const mission = JSON.parse(readFileSync(missionFile, 'utf-8'))

    // Check if already completed/aborted
    if (mission.status === 'completed') {
      return {
        success: false,
        error: 'Mission is already completed',
        missionId: mission.id,
        timestamp: new Date().toISOString(),
      }
    }

    if (mission.status === 'aborted' && !options.force) {
      return {
        success: false,
        error: 'Mission is already aborted. Use --force to abort again.',
        missionId: mission.id,
        timestamp: new Date().toISOString(),
      }
    }

    // Count tasks that will be cancelled
    let tasksAborted = 0
    let tasksCompleted = 0
    const cancelledTasks: string[] = []

    for (const objective of mission.objectives || []) {
      for (const task of objective.tasks || []) {
        if (task.status === 'completed') {
          tasksCompleted++
        } else if (
          task.status === 'pending' ||
          task.status === 'in_progress' ||
          task.status === 'blocked'
        ) {
          tasksAborted++
          cancelledTasks.push(task.id)
          task.status = 'failed'
          task.error = `Aborted: ${options.reason || 'User requested abort'}`
        }
      }
    }

    // Update mission status
    const previousStatus = mission.status
    mission.status = 'aborted'
    mission.abortedAt = new Date().toISOString()
    mission.abortReason = options.reason || 'User requested abort'

    // Create checkpoint if requested
    let checkpointId: string | undefined
    if (options.createCheckpoint) {
      checkpointId = await createAbortCheckpoint(cwd, mission)
    }

    // Save updated mission
    writeFileSync(missionFile, JSON.stringify(mission, null, 2))

    // Log abort event
    logAbortEvent(cwd, {
      missionId: mission.id,
      previousStatus,
      reason: options.reason,
      tasksAborted,
      checkpointId,
    })

    return {
      success: true,
      missionId: mission.id,
      missionTitle: mission.title,
      previousStatus,
      tasksAborted,
      tasksCompleted,
      cancelledTasks,
      checkpointId,
      reason: options.reason || 'User requested abort',
      timestamp: new Date().toISOString(),
    }
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : String(error),
      timestamp: new Date().toISOString(),
    }
  }
}

// =============================================================================
// Checkpoint Creation
// =============================================================================

async function createAbortCheckpoint(cwd: string, mission: unknown): Promise<string> {
  const checkpointsDir = join(cwd, '.delta9', 'checkpoints')

  // Ensure checkpoints directory exists
  if (!existsSync(checkpointsDir)) {
    mkdirSync(checkpointsDir, { recursive: true })
  }

  // Create checkpoint
  const checkpointId = `abort_${Date.now()}`
  const checkpointFile = join(checkpointsDir, `${checkpointId}.json`)

  const checkpoint = {
    id: checkpointId,
    type: 'abort',
    mission,
    createdAt: new Date().toISOString(),
    recoverable: true,
  }

  writeFileSync(checkpointFile, JSON.stringify(checkpoint, null, 2))

  return checkpointId
}

// =============================================================================
// Event Logging
// =============================================================================

function logAbortEvent(
  cwd: string,
  data: {
    missionId: string
    previousStatus: string
    reason?: string
    tasksAborted: number
    checkpointId?: string
  }
): void {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')

  const event = {
    type: 'mission.aborted',
    timestamp: new Date().toISOString(),
    ...data,
  }

  try {
    const line = JSON.stringify(event) + '\n'
    if (existsSync(eventsFile)) {
      const { appendFileSync } = require('node:fs')
      appendFileSync(eventsFile, line)
    }
  } catch {
    // Ignore logging errors
  }
}

// =============================================================================
// Output Formatting
// =============================================================================

function printAbortResult(result: AbortResult): void {
  console.log('')

  if (!result.success) {
    console.log(colorize(`${symbols.error} Abort Failed`, 'red'))
    console.log('')
    console.log(`  ${colorize('Error:', 'red')} ${result.error}`)
    console.log('')
    return
  }

  // Success banner
  console.log(colorize(`${symbols.warning} Mission Aborted`, 'yellow'))
  console.log('')

  // Mission info
  console.log(`  ${colorize('Mission:', 'bold')} ${result.missionTitle || result.missionId}`)
  console.log(`  ${colorize('Previous Status:', 'dim')} ${result.previousStatus}`)
  console.log(`  ${colorize('Reason:', 'dim')} ${result.reason}`)
  console.log('')

  // Task summary
  console.log(`  ${colorize('Tasks Summary:', 'bold')}`)
  console.log(
    `    ${symbols.success} Completed: ${colorize(String(result.tasksCompleted), 'green')}`
  )
  console.log(`    ${symbols.error} Aborted: ${colorize(String(result.tasksAborted), 'red')}`)

  if (
    result.cancelledTasks &&
    result.cancelledTasks.length > 0 &&
    result.cancelledTasks.length <= 5
  ) {
    console.log('')
    console.log(`  ${colorize('Cancelled Tasks:', 'dim')}`)
    for (const taskId of result.cancelledTasks) {
      console.log(`    - ${taskId}`)
    }
  }

  console.log('')

  // Checkpoint info
  if (result.checkpointId) {
    console.log(`  ${colorize('Checkpoint:', 'cyan')} ${result.checkpointId}`)
    console.log(`  ${colorize('Tip:', 'dim')} Use "delta9 resume ${result.checkpointId}" to resume`)
    console.log('')
  }
}
