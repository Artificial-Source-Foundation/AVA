/**
 * Delta9 Resume Command
 *
 * Resume an aborted or paused mission:
 * - Load from checkpoint
 * - Restore mission state
 * - Reset failed tasks for retry
 */

import { existsSync, readFileSync, writeFileSync, readdirSync } from 'node:fs'
import { join } from 'node:path'
import type { ResumeOptions, ResumeResult } from '../types.js'
import { colorize, symbols } from '../types.js'

// =============================================================================
// Resume Command
// =============================================================================

export async function resumeCommand(options: ResumeOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'summary'

  // Execute resume
  const result = await executeResume(cwd, {
    checkpointId: options.checkpoint,
    resetFailed: options.resetFailed !== false,
  })

  // Output based on format
  switch (format) {
    case 'json':
      console.log(JSON.stringify(result, null, 2))
      break
    case 'summary':
    default:
      printResumeResult(result)
      break
  }

  // Exit with error code if resume failed
  if (!result.success) {
    process.exit(1)
  }
}

// =============================================================================
// Resume Execution
// =============================================================================

interface ResumeExecutionOptions {
  checkpointId?: string
  resetFailed?: boolean
}

async function executeResume(cwd: string, options: ResumeExecutionOptions): Promise<ResumeResult> {
  const missionFile = join(cwd, '.delta9', 'mission.json')
  const checkpointsDir = join(cwd, '.delta9', 'checkpoints')

  // Try to find checkpoint
  let checkpoint: unknown | null = null
  let checkpointId = options.checkpointId

  if (checkpointId) {
    // Load specific checkpoint
    const checkpointFile = join(checkpointsDir, `${checkpointId}.json`)
    if (!existsSync(checkpointFile)) {
      return {
        success: false,
        error: `Checkpoint not found: ${checkpointId}`,
        timestamp: new Date().toISOString(),
      }
    }
    checkpoint = JSON.parse(readFileSync(checkpointFile, 'utf-8'))
  } else {
    // Find most recent checkpoint
    if (existsSync(checkpointsDir)) {
      const checkpoints = readdirSync(checkpointsDir)
        .filter((f) => f.endsWith('.json'))
        .map((f) => ({
          name: f,
          path: join(checkpointsDir, f),
          data: JSON.parse(readFileSync(join(checkpointsDir, f), 'utf-8')),
        }))
        .filter((c) => c.data.recoverable !== false)
        .sort((a, b) => {
          const timeA = new Date(a.data.createdAt).getTime()
          const timeB = new Date(b.data.createdAt).getTime()
          return timeB - timeA
        })

      if (checkpoints.length > 0) {
        checkpoint = checkpoints[0].data
        checkpointId = checkpoints[0].data.id
      }
    }
  }

  // If we have a checkpoint, restore from it
  if (checkpoint && typeof checkpoint === 'object' && 'mission' in checkpoint) {
    const cp = checkpoint as { mission: unknown; id: string; type: string }
    return resumeFromCheckpoint(cwd, cp, options)
  }

  // Otherwise, try to resume the current mission
  if (!existsSync(missionFile)) {
    return {
      success: false,
      error: 'No mission or checkpoint found to resume',
      availableCheckpoints: listAvailableCheckpoints(checkpointsDir),
      timestamp: new Date().toISOString(),
    }
  }

  return resumeCurrentMission(cwd, options)
}

// =============================================================================
// Resume From Checkpoint
// =============================================================================

async function resumeFromCheckpoint(
  cwd: string,
  checkpoint: { mission: unknown; id: string; type: string },
  options: ResumeExecutionOptions
): Promise<ResumeResult> {
  const missionFile = join(cwd, '.delta9', 'mission.json')

  try {
    const mission = checkpoint.mission as Record<string, unknown>

    // Reset failed tasks if requested
    let tasksReset = 0
    const resetTasks: string[] = []

    if (options.resetFailed) {
      for (const objective of (mission.objectives as Array<{
        tasks: Array<{ id: string; status: string; error?: string }>
      }>) || []) {
        for (const task of objective.tasks || []) {
          if (task.status === 'failed') {
            task.status = 'pending'
            delete task.error
            tasksReset++
            resetTasks.push(task.id)
          }
        }
      }
    }

    // Update mission status
    const previousStatus = mission.status as string
    mission.status = 'paused' // Ready to be resumed by Commander
    mission.resumedAt = new Date().toISOString()
    mission.resumedFromCheckpoint = checkpoint.id

    // Save mission
    writeFileSync(missionFile, JSON.stringify(mission, null, 2))

    // Log resume event
    logResumeEvent(cwd, {
      missionId: mission.id as string,
      checkpointId: checkpoint.id,
      previousStatus,
      tasksReset,
    })

    // Count tasks
    const { pending, inProgress, completed } = countTasks(mission)

    return {
      success: true,
      missionId: mission.id as string,
      missionTitle: mission.title as string,
      checkpointId: checkpoint.id,
      checkpointType: checkpoint.type,
      previousStatus,
      newStatus: 'paused',
      tasksReset,
      resetTasks: resetTasks.length <= 5 ? resetTasks : undefined,
      taskSummary: { pending, inProgress, completed },
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
// Resume Current Mission
// =============================================================================

async function resumeCurrentMission(
  cwd: string,
  options: ResumeExecutionOptions
): Promise<ResumeResult> {
  const missionFile = join(cwd, '.delta9', 'mission.json')

  try {
    const mission = JSON.parse(readFileSync(missionFile, 'utf-8'))

    // Check if mission can be resumed
    if (mission.status === 'completed') {
      return {
        success: false,
        error: 'Mission is already completed',
        missionId: mission.id,
        timestamp: new Date().toISOString(),
      }
    }

    if (mission.status === 'in_progress') {
      return {
        success: false,
        error: 'Mission is already in progress',
        missionId: mission.id,
        timestamp: new Date().toISOString(),
      }
    }

    // Reset failed tasks if requested
    let tasksReset = 0
    const resetTasks: string[] = []

    if (options.resetFailed) {
      for (const objective of mission.objectives || []) {
        for (const task of objective.tasks || []) {
          if (task.status === 'failed') {
            task.status = 'pending'
            delete task.error
            tasksReset++
            resetTasks.push(task.id)
          }
        }
      }
    }

    // Update mission status
    const previousStatus = mission.status
    mission.status = 'paused' // Ready to be resumed by Commander
    mission.resumedAt = new Date().toISOString()

    // Save mission
    writeFileSync(missionFile, JSON.stringify(mission, null, 2))

    // Log resume event
    logResumeEvent(cwd, {
      missionId: mission.id,
      previousStatus,
      tasksReset,
    })

    // Count tasks
    const { pending, inProgress, completed } = countTasks(mission)

    return {
      success: true,
      missionId: mission.id,
      missionTitle: mission.title,
      previousStatus,
      newStatus: 'paused',
      tasksReset,
      resetTasks: resetTasks.length <= 5 ? resetTasks : undefined,
      taskSummary: { pending, inProgress, completed },
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
// Utilities
// =============================================================================

function countTasks(mission: Record<string, unknown>): {
  pending: number
  inProgress: number
  completed: number
} {
  let pending = 0
  let inProgress = 0
  let completed = 0

  for (const objective of (mission.objectives as Array<{ tasks: Array<{ status: string }> }>) ||
    []) {
    for (const task of objective.tasks || []) {
      switch (task.status) {
        case 'pending':
        case 'blocked':
          pending++
          break
        case 'in_progress':
          inProgress++
          break
        case 'completed':
          completed++
          break
      }
    }
  }

  return { pending, inProgress, completed }
}

function listAvailableCheckpoints(checkpointsDir: string): string[] {
  if (!existsSync(checkpointsDir)) {
    return []
  }

  return readdirSync(checkpointsDir)
    .filter((f) => f.endsWith('.json'))
    .map((f) => f.replace('.json', ''))
    .slice(0, 5)
}

function logResumeEvent(
  cwd: string,
  data: {
    missionId: string
    checkpointId?: string
    previousStatus: string
    tasksReset: number
  }
): void {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')

  const event = {
    type: 'mission.resumed',
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

function printResumeResult(result: ResumeResult): void {
  console.log('')

  if (!result.success) {
    console.log(colorize(`${symbols.error} Resume Failed`, 'red'))
    console.log('')
    console.log(`  ${colorize('Error:', 'red')} ${result.error}`)

    if (result.availableCheckpoints && result.availableCheckpoints.length > 0) {
      console.log('')
      console.log(`  ${colorize('Available Checkpoints:', 'dim')}`)
      for (const cp of result.availableCheckpoints) {
        console.log(`    - ${cp}`)
      }
      console.log('')
      console.log(
        `  ${colorize('Tip:', 'dim')} Use "delta9 resume <checkpoint-id>" to resume from a checkpoint`
      )
    }

    console.log('')
    return
  }

  // Success banner
  console.log(colorize(`${symbols.success} Mission Resumed`, 'green'))
  console.log('')

  // Mission info
  console.log(`  ${colorize('Mission:', 'bold')} ${result.missionTitle || result.missionId}`)

  if (result.checkpointId) {
    console.log(
      `  ${colorize('Checkpoint:', 'cyan')} ${result.checkpointId} (${result.checkpointType})`
    )
  }

  console.log(`  ${colorize('Previous Status:', 'dim')} ${result.previousStatus}`)
  console.log(`  ${colorize('New Status:', 'green')} ${result.newStatus}`)
  console.log('')

  // Task summary
  if (result.taskSummary) {
    console.log(`  ${colorize('Tasks:', 'bold')}`)
    console.log(
      `    ${symbols.pending} Pending: ${colorize(String(result.taskSummary.pending), 'yellow')}`
    )
    console.log(
      `    ${symbols.inProgress} In Progress: ${colorize(String(result.taskSummary.inProgress), 'blue')}`
    )
    console.log(
      `    ${symbols.success} Completed: ${colorize(String(result.taskSummary.completed), 'green')}`
    )
  }

  // Reset tasks
  if (result.tasksReset && result.tasksReset > 0) {
    console.log('')
    console.log(`  ${colorize('Tasks Reset:', 'yellow')} ${result.tasksReset}`)

    if (result.resetTasks && result.resetTasks.length > 0) {
      for (const taskId of result.resetTasks) {
        console.log(`    - ${taskId}`)
      }
    }
  }

  console.log('')
  console.log(
    `  ${colorize('Tip:', 'dim')} The mission is now ready to be resumed by the Commander`
  )
  console.log('')
}
