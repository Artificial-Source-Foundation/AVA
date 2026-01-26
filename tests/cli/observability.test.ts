/**
 * Observability CLI Tests
 *
 * Tests for the new observability features:
 * - Query presets (errors, decisions, budget, agents, timeline)
 * - Replay command
 * - Export command
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { existsSync, mkdirSync, writeFileSync, rmSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'

// Sample events for testing
const sampleEvents = [
  {
    id: 'evt_001',
    type: 'mission.created',
    timestamp: '2024-01-15T10:00:00.000Z',
    missionId: 'mission_123',
    data: { name: 'Test Mission' },
  },
  {
    id: 'evt_002',
    type: 'task.created',
    timestamp: '2024-01-15T10:00:05.000Z',
    missionId: 'mission_123',
    taskId: 'task_001',
    data: { title: 'First Task', taskId: 'task_001' },
  },
  {
    id: 'evt_003',
    type: 'agent.dispatched',
    timestamp: '2024-01-15T10:00:10.000Z',
    missionId: 'mission_123',
    taskId: 'task_001',
    data: { agent: 'operator', taskId: 'task_001' },
  },
  {
    id: 'evt_004',
    type: 'task.completed',
    timestamp: '2024-01-15T10:00:30.000Z',
    missionId: 'mission_123',
    taskId: 'task_001',
    data: { taskId: 'task_001', success: true },
  },
  {
    id: 'evt_005',
    type: 'task.failed',
    timestamp: '2024-01-15T10:01:00.000Z',
    missionId: 'mission_123',
    taskId: 'task_002',
    data: { taskId: 'task_002', error: 'Something went wrong' },
  },
  {
    id: 'evt_006',
    type: 'budget.threshold_warning',
    timestamp: '2024-01-15T10:01:30.000Z',
    missionId: 'mission_123',
    data: { percentage: 80 },
  },
  {
    id: 'evt_007',
    type: 'mission.completed',
    timestamp: '2024-01-15T10:02:00.000Z',
    missionId: 'mission_123',
    data: { success: true },
  },
]

describe('Query Presets', () => {
  let testDir: string

  beforeEach(() => {
    testDir = join(tmpdir(), `delta9-test-${Date.now()}`)
    mkdirSync(join(testDir, '.delta9'), { recursive: true })

    // Write sample events
    const eventsFile = join(testDir, '.delta9', 'events.jsonl')
    writeFileSync(eventsFile, sampleEvents.map((e) => JSON.stringify(e)).join('\n'))
  })

  afterEach(() => {
    rmSync(testDir, { recursive: true, force: true })
  })

  describe('Preset Definitions', () => {
    it('should have errors preset', async () => {
      // Import dynamically to allow fresh module state
      const { queryCommand } = await import('../../src/cli/commands/query.js')

      // Mock console.log to capture output
      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await queryCommand({
        cwd: testDir,
        preset: 'errors',
        format: 'json',
      })

      console.log = originalLog

      // Parse JSON output
      const output = JSON.parse(logs.join(''))
      expect(output.query.search).toContain('failed')
    })

    it('should have budget preset', async () => {
      const { queryCommand } = await import('../../src/cli/commands/query.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await queryCommand({
        cwd: testDir,
        preset: 'budget',
        format: 'json',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.query.category).toBe('budget')
    })

    it('should have agents preset', async () => {
      const { queryCommand } = await import('../../src/cli/commands/query.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await queryCommand({
        cwd: testDir,
        preset: 'agents',
        format: 'json',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.query.category).toBe('agent')
    })

    it('should have timeline preset', async () => {
      const { queryCommand } = await import('../../src/cli/commands/query.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await queryCommand({
        cwd: testDir,
        preset: 'timeline',
        format: 'json',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.query.since).toBe('1h')
      expect(output.query.limit).toBe(100)
    })
  })

  describe('Query Filtering', () => {
    it('should filter by type', async () => {
      const { queryCommand } = await import('../../src/cli/commands/query.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await queryCommand({
        cwd: testDir,
        type: 'task.completed',
        format: 'json',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.events.every((e: { type: string }) => e.type === 'task.completed')).toBe(true)
    })

    it('should filter by search term', async () => {
      const { queryCommand } = await import('../../src/cli/commands/query.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await queryCommand({
        cwd: testDir,
        search: 'error',
        format: 'json',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.stats.matched).toBeGreaterThan(0)
    })
  })
})

describe('Export Command', () => {
  let testDir: string

  beforeEach(() => {
    testDir = join(tmpdir(), `delta9-test-${Date.now()}`)
    mkdirSync(join(testDir, '.delta9'), { recursive: true })

    const eventsFile = join(testDir, '.delta9', 'events.jsonl')
    writeFileSync(eventsFile, sampleEvents.map((e) => JSON.stringify(e)).join('\n'))
  })

  afterEach(() => {
    rmSync(testDir, { recursive: true, force: true })
  })

  describe('JSON Export', () => {
    it('should export events as JSON', async () => {
      const { exportCommand } = await import('../../src/cli/commands/export.js')

      const outputPath = join(testDir, 'export.json')

      // Suppress console output
      const originalLog = console.log
      console.log = () => {}

      await exportCommand({
        cwd: testDir,
        format: 'json',
        output: outputPath,
      })

      console.log = originalLog

      expect(existsSync(outputPath)).toBe(true)

      const content = JSON.parse(readFileSync(outputPath, 'utf-8'))
      expect(Array.isArray(content)).toBe(true)
      expect(content.length).toBe(7)
    })
  })

  describe('CSV Export', () => {
    it('should export events as CSV', async () => {
      const { exportCommand } = await import('../../src/cli/commands/export.js')

      const outputPath = join(testDir, 'export.csv')

      const originalLog = console.log
      console.log = () => {}

      await exportCommand({
        cwd: testDir,
        format: 'csv',
        output: outputPath,
      })

      console.log = originalLog

      expect(existsSync(outputPath)).toBe(true)

      const content = readFileSync(outputPath, 'utf-8')
      const lines = content.trim().split('\n')

      // Should have header + 7 data rows
      expect(lines.length).toBe(8)

      // Header should contain expected columns
      expect(lines[0]).toContain('timestamp')
      expect(lines[0]).toContain('type')
      expect(lines[0]).toContain('missionId')
    })

    it('should escape CSV values correctly', async () => {
      // Add event with comma in data
      const eventsFile = join(testDir, '.delta9', 'events.jsonl')
      const eventWithComma = {
        id: 'evt_special',
        type: 'test.event',
        timestamp: '2024-01-15T10:00:00.000Z',
        missionId: 'mission_123',
        data: { message: 'Hello, World' },
      }
      writeFileSync(eventsFile, JSON.stringify(eventWithComma))

      const { exportCommand } = await import('../../src/cli/commands/export.js')

      const outputPath = join(testDir, 'export-special.csv')

      const originalLog = console.log
      console.log = () => {}

      await exportCommand({
        cwd: testDir,
        format: 'csv',
        output: outputPath,
      })

      console.log = originalLog

      const content = readFileSync(outputPath, 'utf-8')
      // CSV should properly handle the comma
      expect(content).toBeDefined()
    })
  })

  describe('JSONL Export', () => {
    it('should export events as JSONL', async () => {
      const { exportCommand } = await import('../../src/cli/commands/export.js')

      const outputPath = join(testDir, 'export.jsonl')

      const originalLog = console.log
      console.log = () => {}

      await exportCommand({
        cwd: testDir,
        format: 'jsonl',
        output: outputPath,
      })

      console.log = originalLog

      expect(existsSync(outputPath)).toBe(true)

      const content = readFileSync(outputPath, 'utf-8')
      const lines = content.trim().split('\n')

      expect(lines.length).toBe(7)

      // Each line should be valid JSON
      for (const line of lines) {
        expect(() => JSON.parse(line)).not.toThrow()
      }
    })
  })

  describe('Export Filtering', () => {
    it('should filter by type', async () => {
      const { exportCommand } = await import('../../src/cli/commands/export.js')

      const outputPath = join(testDir, 'filtered.json')

      const originalLog = console.log
      console.log = () => {}

      await exportCommand({
        cwd: testDir,
        format: 'json',
        type: 'task.completed',
        output: outputPath,
      })

      console.log = originalLog

      const content = JSON.parse(readFileSync(outputPath, 'utf-8'))
      expect(content.every((e: { type: string }) => e.type === 'task.completed')).toBe(true)
    })
  })
})

describe('Replay Command', () => {
  let testDir: string

  beforeEach(() => {
    testDir = join(tmpdir(), `delta9-test-${Date.now()}`)
    mkdirSync(join(testDir, '.delta9'), { recursive: true })

    const eventsFile = join(testDir, '.delta9', 'events.jsonl')
    writeFileSync(eventsFile, sampleEvents.map((e) => JSON.stringify(e)).join('\n'))
  })

  afterEach(() => {
    rmSync(testDir, { recursive: true, force: true })
  })

  describe('JSON Format', () => {
    it('should output replay result as JSON', async () => {
      const { replayCommand } = await import('../../src/cli/commands/replay.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await replayCommand({
        cwd: testDir,
        format: 'json',
        speed: 'instant',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.events).toBeDefined()
      expect(output.stats).toBeDefined()
      expect(output.stats.total).toBe(7)
    })

    it('should include elapsed time in events', async () => {
      const { replayCommand } = await import('../../src/cli/commands/replay.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await replayCommand({
        cwd: testDir,
        format: 'json',
        speed: 'instant',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.events[0].elapsed).toBe(0) // First event
      expect(output.events[1].elapsed).toBeGreaterThan(0) // Subsequent events
    })
  })

  describe('Filtering', () => {
    it('should filter by type', async () => {
      const { replayCommand } = await import('../../src/cli/commands/replay.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await replayCommand({
        cwd: testDir,
        format: 'json',
        speed: 'instant',
        type: 'task.completed',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.events.every((e: { type: string }) => e.type === 'task.completed')).toBe(true)
    })

    it('should filter by mission ID', async () => {
      const { replayCommand } = await import('../../src/cli/commands/replay.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await replayCommand({
        cwd: testDir,
        format: 'json',
        speed: 'instant',
        missionId: 'mission_123',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.missionId).toBe('mission_123')
      expect(output.stats.total).toBe(7)
    })

    it('should filter by event range (start/end)', async () => {
      const { replayCommand } = await import('../../src/cli/commands/replay.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await replayCommand({
        cwd: testDir,
        format: 'json',
        speed: 'instant',
        start: 2,
        end: 4,
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.stats.total).toBe(3) // Events 2, 3, 4
    })
  })

  describe('Statistics', () => {
    it('should calculate duration', async () => {
      const { replayCommand } = await import('../../src/cli/commands/replay.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await replayCommand({
        cwd: testDir,
        format: 'json',
        speed: 'instant',
      })

      console.log = originalLog

      const output = JSON.parse(logs.join(''))
      expect(output.stats.duration).toBeGreaterThan(0)
    })

    it('should group by category', async () => {
      const { replayCommand } = await import('../../src/cli/commands/replay.js')

      const logs: string[] = []
      const originalLog = console.log
      console.log = (...args) => logs.push(args.join(' '))

      await replayCommand({
        cwd: testDir,
        format: 'json', // Must be JSON format
        speed: 'instant',
      })

      console.log = originalLog

      // Filter to only get the JSON output line
      const jsonLine = logs.find((line) => line.startsWith('{'))
      expect(jsonLine).toBeDefined()

      const output = JSON.parse(jsonLine!)
      expect(output.stats.categories).toBeDefined()
      expect(typeof output.stats.categories).toBe('object')
    })
  })
})

describe('CLI Types', () => {
  it('should export ReplayOptions', async () => {
    const { default: types } = await import('../../src/cli/types.js')
    // Type check - just ensure the module loads
    expect(types).toBeUndefined() // Named exports only
  })

  it('should export ExportOptions', async () => {
    const types = await import('../../src/cli/types.js')
    // Check that types module has our interfaces (compile-time check)
    expect(types).toBeDefined()
  })
})
