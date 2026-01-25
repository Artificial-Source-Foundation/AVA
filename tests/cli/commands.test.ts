/**
 * Tests for Delta9 CLI Commands
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { existsSync, mkdirSync, writeFileSync, rmSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'
import { statusCommand } from '../../src/cli/commands/status.js'
import { historyCommand } from '../../src/cli/commands/history.js'
import { healthCommand } from '../../src/cli/commands/health.js'
import { abortCommand } from '../../src/cli/commands/abort.js'
import { resumeCommand } from '../../src/cli/commands/resume.js'

// =============================================================================
// Test Setup
// =============================================================================

let testDir: string
let originalLog: typeof console.log

beforeEach(() => {
  // Create temp test directory
  testDir = join(tmpdir(), `delta9-cli-test-${Date.now()}`)
  mkdirSync(testDir, { recursive: true })
  mkdirSync(join(testDir, '.delta9'), { recursive: true })

  // Capture console output
  originalLog = console.log
})

afterEach(() => {
  // Restore console
  console.log = originalLog

  // Cleanup temp directory
  if (existsSync(testDir)) {
    rmSync(testDir, { recursive: true, force: true })
  }
})

// =============================================================================
// Status Command Tests
// =============================================================================

describe('statusCommand', () => {
  it('should show no active mission when mission.json does not exist', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await statusCommand({ cwd: testDir, format: 'summary' })

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('No active mission')
  })

  it('should display mission when mission.json exists', async () => {
    // Create mission file
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'test-mission-1',
        title: 'Test Mission',
        status: 'active',
        objectives: [
          {
            id: 'obj-1',
            description: 'First objective',
            tasks: [
              { id: 'task-1', description: 'Task 1', status: 'completed' },
              { id: 'task-2', description: 'Task 2', status: 'pending' },
            ],
          },
        ],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await statusCommand({ cwd: testDir, format: 'summary' })

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('Test Mission')
    expect(fullOutput).toContain('active')
  })

  it('should output JSON format', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await statusCommand({ cwd: testDir, format: 'json' })

    const fullOutput = output.join('')
    const parsed = JSON.parse(fullOutput)

    expect(parsed).toHaveProperty('mission')
    expect(parsed).toHaveProperty('tasks')
    expect(parsed).toHaveProperty('background')
    expect(parsed.mission.active).toBe(false)
  })

  it('should output table format', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await statusCommand({ cwd: testDir, format: 'table' })

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('Mission')
    expect(fullOutput).toContain('Active')
    expect(fullOutput).toContain('│')
  })

  it('should count task statuses correctly', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'test-mission',
        status: 'active',
        objectives: [
          {
            id: 'obj-1',
            tasks: [
              { id: 't1', status: 'completed' },
              { id: 't2', status: 'completed' },
              { id: 't3', status: 'pending' },
              { id: 't4', status: 'in_progress' },
              { id: 't5', status: 'failed' },
            ],
          },
        ],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await statusCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.tasks.completed).toBe(2)
    expect(parsed.tasks.pending).toBe(1)
    expect(parsed.tasks.inProgress).toBe(1)
    expect(parsed.tasks.failed).toBe(1)
    expect(parsed.mission.progress.percentage).toBe(40) // 2/5 = 40%
  })
})

// =============================================================================
// History Command Tests
// =============================================================================

describe('historyCommand', () => {
  it('should show no events when events.jsonl does not exist', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await historyCommand({ cwd: testDir, format: 'timeline', limit: 20 })

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('No events')
  })

  it('should display events from events.jsonl', async () => {
    // Create events file
    const events = [
      { id: 'e1', type: 'mission.created', timestamp: new Date().toISOString(), data: { title: 'Test' } },
      { id: 'e2', type: 'task.started', timestamp: new Date().toISOString(), data: { taskId: 't1' } },
    ]
    writeFileSync(
      join(testDir, '.delta9', 'events.jsonl'),
      events.map((e) => JSON.stringify(e)).join('\n')
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await historyCommand({ cwd: testDir, format: 'timeline', limit: 20 })

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('mission.created')
    expect(fullOutput).toContain('task.started')
  })

  it('should output JSON format', async () => {
    const events = [
      { id: 'e1', type: 'mission.created', timestamp: new Date().toISOString(), data: {} },
    ]
    writeFileSync(
      join(testDir, '.delta9', 'events.jsonl'),
      events.map((e) => JSON.stringify(e)).join('\n')
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await historyCommand({ cwd: testDir, format: 'json', limit: 20 })

    const parsed = JSON.parse(output.join(''))
    expect(parsed).toHaveProperty('events')
    expect(parsed).toHaveProperty('stats')
    expect(parsed.events).toHaveLength(1)
  })

  it('should filter by type', async () => {
    const events = [
      { id: 'e1', type: 'mission.created', timestamp: new Date().toISOString(), data: {} },
      { id: 'e2', type: 'task.started', timestamp: new Date().toISOString(), data: {} },
      { id: 'e3', type: 'mission.completed', timestamp: new Date().toISOString(), data: {} },
    ]
    writeFileSync(
      join(testDir, '.delta9', 'events.jsonl'),
      events.map((e) => JSON.stringify(e)).join('\n')
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await historyCommand({ cwd: testDir, format: 'json', type: 'mission', limit: 20 })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.events).toHaveLength(2)
    expect(parsed.events.every((e: { type: string }) => e.type.startsWith('mission.'))).toBe(true)
  })

  it('should respect limit option', async () => {
    const events = []
    for (let i = 0; i < 10; i++) {
      events.push({
        id: `e${i}`,
        type: 'task.started',
        timestamp: new Date().toISOString(),
        data: {},
      })
    }
    writeFileSync(
      join(testDir, '.delta9', 'events.jsonl'),
      events.map((e) => JSON.stringify(e)).join('\n')
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await historyCommand({ cwd: testDir, format: 'json', limit: 5 })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.events).toHaveLength(5)
  })

  it('should filter by category', async () => {
    const events = [
      { id: 'e1', type: 'mission.created', timestamp: new Date().toISOString(), data: {} },
      { id: 'e2', type: 'council.started', timestamp: new Date().toISOString(), data: {} },
      { id: 'e3', type: 'agent.dispatched', timestamp: new Date().toISOString(), data: {} },
    ]
    writeFileSync(
      join(testDir, '.delta9', 'events.jsonl'),
      events.map((e) => JSON.stringify(e)).join('\n')
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await historyCommand({ cwd: testDir, format: 'json', category: 'council', limit: 20 })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.events).toHaveLength(1)
    expect(parsed.events[0].type).toBe('council.started')
  })
})

// =============================================================================
// Health Command Tests
// =============================================================================

describe('healthCommand', () => {
  it('should run health checks and show summary', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'summary' })

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('Delta9 Health Check')
    expect(fullOutput).toContain('Configuration')
    expect(fullOutput).toContain('Summary')
  })

  it('should output JSON format', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    expect(parsed).toHaveProperty('report')
    expect(parsed).toHaveProperty('checks')
    expect(parsed.report).toHaveProperty('status')
    expect(Array.isArray(parsed.checks)).toBe(true)
  })

  it('should pass .delta9 directory check when directory exists', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    const dirCheck = parsed.checks.find((c: { name: string }) => c.name === '.delta9 directory')
    expect(dirCheck?.status).toBe('pass')
  })

  it('should check mission state when mission exists', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({ id: 'test', status: 'active', objectives: [] })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    const missionCheck = parsed.checks.find((c: { name: string }) => c.name === 'Mission state')
    expect(missionCheck?.status).toBe('pass')
    expect(missionCheck?.message).toContain('Active mission')
  })

  it('should warn about missing config file', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    const configCheck = parsed.checks.find((c: { name: string }) => c.name === 'Configuration file')
    expect(configCheck?.status).toBe('warn')
    expect(configCheck?.message).toContain('No delta9.json')
  })

  it('should pass config check when delta9.json exists', async () => {
    writeFileSync(
      join(testDir, 'delta9.json'),
      JSON.stringify({ council: { enabled: true } })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    const configCheck = parsed.checks.find((c: { name: string }) => c.name === 'Configuration file')
    expect(configCheck?.status).toBe('pass')
    expect(configCheck?.message).toContain('Found and valid')
  })

  it('should fail on invalid JSON config', async () => {
    writeFileSync(join(testDir, 'delta9.json'), 'invalid json {{{')

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    const configCheck = parsed.checks.find((c: { name: string }) => c.name === 'Configuration file')
    expect(configCheck?.status).toBe('fail')
  })

  it('should check Node.js version', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    const nodeCheck = parsed.checks.find((c: { name: string }) => c.name === 'Node.js version')
    expect(['pass', 'warn']).toContain(nodeCheck?.status) // Pass if >= 20, warn if >= 18 < 20
    expect(nodeCheck?.message).toMatch(/^v\d+/)
  })

  it('should include verbose checks when verbose option is set', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json', verbose: true })

    const parsed = JSON.parse(output.join(''))
    const checkNames = parsed.checks.map((c: { name: string }) => c.name)
    expect(checkNames).toContain('Disk space')
    expect(checkNames).toContain('Recent activity')
  })

  it('should determine overall health status', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await healthCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    expect(['healthy', 'degraded', 'unhealthy']).toContain(parsed.report.status)
  })
})

// =============================================================================
// Type Definitions Tests
// =============================================================================

// =============================================================================
// Abort Command Tests
// =============================================================================

describe('abortCommand', () => {
  it('should fail when no active mission exists', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    // Mock process.exit to prevent test from exiting
    const mockExit = vi.spyOn(process, 'exit').mockImplementation(() => {
      throw new Error('process.exit called')
    })

    try {
      await abortCommand({ cwd: testDir, format: 'summary' })
    } catch {
      // Expected
    }

    mockExit.mockRestore()

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('No active mission')
  })

  it('should abort an active mission', async () => {
    // Create active mission
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'test-abort-1',
        title: 'Mission to Abort',
        status: 'in_progress',
        objectives: [
          {
            id: 'obj-1',
            tasks: [
              { id: 'task-1', status: 'completed' },
              { id: 'task-2', status: 'pending' },
              { id: 'task-3', status: 'in_progress' },
            ],
          },
        ],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await abortCommand({ cwd: testDir, format: 'summary', reason: 'Test abort' })

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('Mission Aborted')
    expect(fullOutput).toContain('Mission to Abort')
  })

  it('should output JSON format', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'test-abort-2',
        title: 'JSON Abort Test',
        status: 'in_progress',
        objectives: [{ id: 'obj-1', tasks: [{ id: 't1', status: 'pending' }] }],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await abortCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.success).toBe(true)
    expect(parsed.missionId).toBe('test-abort-2')
    expect(parsed.tasksAborted).toBe(1)
  })

  it('should create checkpoint by default', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'test-checkpoint',
        status: 'in_progress',
        objectives: [],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await abortCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.checkpointId).toBeDefined()
    expect(parsed.checkpointId).toContain('abort_')

    // Verify checkpoint file exists
    const checkpointFile = join(testDir, '.delta9', 'checkpoints', `${parsed.checkpointId}.json`)
    expect(existsSync(checkpointFile)).toBe(true)
  })

  it('should count aborted vs completed tasks', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'task-count-test',
        status: 'in_progress',
        objectives: [
          {
            id: 'obj-1',
            tasks: [
              { id: 't1', status: 'completed' },
              { id: 't2', status: 'completed' },
              { id: 't3', status: 'pending' },
              { id: 't4', status: 'in_progress' },
              { id: 't5', status: 'blocked' },
            ],
          },
        ],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await abortCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.tasksCompleted).toBe(2)
    expect(parsed.tasksAborted).toBe(3) // pending + in_progress + blocked
  })
})

// =============================================================================
// Resume Command Tests
// =============================================================================

describe('resumeCommand', () => {
  it('should fail when no mission or checkpoint exists', async () => {
    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    const mockExit = vi.spyOn(process, 'exit').mockImplementation(() => {
      throw new Error('process.exit called')
    })

    try {
      await resumeCommand({ cwd: testDir, format: 'summary' })
    } catch {
      // Expected
    }

    mockExit.mockRestore()

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('No mission or checkpoint')
  })

  it('should resume an aborted mission', async () => {
    // Create aborted mission
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'test-resume-1',
        title: 'Mission to Resume',
        status: 'aborted',
        objectives: [
          {
            id: 'obj-1',
            tasks: [
              { id: 't1', status: 'completed' },
              { id: 't2', status: 'failed', error: 'Previous error' },
            ],
          },
        ],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await resumeCommand({ cwd: testDir, format: 'summary' })

    const fullOutput = output.join('\n')
    expect(fullOutput).toContain('Mission Resumed')
    expect(fullOutput).toContain('Mission to Resume')
  })

  it('should output JSON format', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'test-resume-json',
        title: 'JSON Resume Test',
        status: 'aborted',
        objectives: [{ id: 'obj-1', tasks: [] }],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await resumeCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.success).toBe(true)
    expect(parsed.missionId).toBe('test-resume-json')
    expect(parsed.newStatus).toBe('paused')
  })

  it('should reset failed tasks by default', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'reset-failed-test',
        status: 'aborted',
        objectives: [
          {
            id: 'obj-1',
            tasks: [
              { id: 't1', status: 'completed' },
              { id: 't2', status: 'failed', error: 'Error 1' },
              { id: 't3', status: 'failed', error: 'Error 2' },
            ],
          },
        ],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await resumeCommand({ cwd: testDir, format: 'json' })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.tasksReset).toBe(2)
  })

  it('should not reset failed tasks when resetFailed is false', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'no-reset-test',
        status: 'aborted',
        objectives: [
          {
            id: 'obj-1',
            tasks: [{ id: 't1', status: 'failed', error: 'Keep this error' }],
          },
        ],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await resumeCommand({ cwd: testDir, format: 'json', resetFailed: false })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.tasksReset).toBe(0)
  })

  it('should resume from specific checkpoint', async () => {
    // Create checkpoint
    const checkpointsDir = join(testDir, '.delta9', 'checkpoints')
    mkdirSync(checkpointsDir, { recursive: true })

    const checkpoint = {
      id: 'specific-checkpoint',
      type: 'abort',
      mission: {
        id: 'checkpoint-mission',
        title: 'From Checkpoint',
        status: 'aborted',
        objectives: [{ id: 'obj-1', tasks: [] }],
      },
      createdAt: new Date().toISOString(),
      recoverable: true,
    }

    writeFileSync(
      join(checkpointsDir, 'specific-checkpoint.json'),
      JSON.stringify(checkpoint)
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    await resumeCommand({ cwd: testDir, format: 'json', checkpoint: 'specific-checkpoint' })

    const parsed = JSON.parse(output.join(''))
    expect(parsed.success).toBe(true)
    expect(parsed.checkpointId).toBe('specific-checkpoint')
    expect(parsed.missionId).toBe('checkpoint-mission')
  })

  it('should fail for already completed mission', async () => {
    writeFileSync(
      join(testDir, '.delta9', 'mission.json'),
      JSON.stringify({
        id: 'completed-mission',
        status: 'completed',
        objectives: [],
      })
    )

    const output: string[] = []
    console.log = vi.fn((...args) => output.push(args.join(' ')))

    const mockExit = vi.spyOn(process, 'exit').mockImplementation(() => {
      throw new Error('process.exit called')
    })

    try {
      await resumeCommand({ cwd: testDir, format: 'json' })
    } catch {
      // Expected
    }

    mockExit.mockRestore()

    const parsed = JSON.parse(output.join(''))
    expect(parsed.success).toBe(false)
    expect(parsed.error).toContain('already completed')
  })
})

// =============================================================================
// Types Tests
// =============================================================================

describe('types', () => {
  it('should export colorize function', async () => {
    const { colorize } = await import('../../src/cli/types.js')
    expect(typeof colorize).toBe('function')
  })

  it('should export symbols', async () => {
    const { symbols } = await import('../../src/cli/types.js')
    expect(symbols.check).toBeDefined()
    expect(symbols.cross).toBeDefined()
    expect(symbols.warning).toBeDefined()
    expect(symbols.success).toBeDefined()
    expect(symbols.error).toBeDefined()
  })

  it('should colorize text correctly', async () => {
    const { colorize } = await import('../../src/cli/types.js')
    const result = colorize('test', 'red')
    expect(result).toContain('test')
    expect(result).toContain('\x1b[31m') // Red ANSI code
  })
})
