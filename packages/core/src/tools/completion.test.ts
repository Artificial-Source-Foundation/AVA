/**
 * Completion Tool Tests
 * Tests for task completion signaling and state tracking
 */

import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  cleanupCompletionStates,
  completionTool,
  formatCompletionResult,
  getCompletionDetails,
  isCompletionCall,
  resetCompletionState,
  wasCompletionAttempted,
} from './completion.js'

// Mock the hooks module to avoid filesystem-dependent hook runner
vi.mock('../hooks/index.js', () => ({
  getHookRunner: () => ({
    run: async () => {},
  }),
  createTaskCompleteContext: (ctx: Record<string, unknown>) => ctx,
}))

// Clean up completion state between tests
afterEach(() => {
  resetCompletionState('test-session')
  resetCompletionState('session-1')
  resetCompletionState('session-2')
  resetCompletionState('old-session')
  resetCompletionState('recent-session')
})

// ============================================================================
// Helpers
// ============================================================================

/** Create a minimal ToolContext for testing */
function createTestContext(sessionId = 'test-session') {
  return {
    sessionId,
    workingDirectory: '/tmp',
    signal: new AbortController().signal,
  }
}

// ============================================================================
// isCompletionCall
// ============================================================================

describe('isCompletionCall', () => {
  it('should return true for "attempt_completion"', () => {
    expect(isCompletionCall('attempt_completion')).toBe(true)
  })

  it('should return false for "bash"', () => {
    expect(isCompletionCall('bash')).toBe(false)
  })

  it('should return false for empty string', () => {
    expect(isCompletionCall('')).toBe(false)
  })

  it('should return false for partial match', () => {
    expect(isCompletionCall('attempt_completion_extra')).toBe(false)
  })

  it('should return false for case-different match', () => {
    expect(isCompletionCall('Attempt_Completion')).toBe(false)
  })

  it('should return false for other tool names', () => {
    expect(isCompletionCall('read_file')).toBe(false)
    expect(isCompletionCall('write_file')).toBe(false)
    expect(isCompletionCall('edit')).toBe(false)
    expect(isCompletionCall('glob')).toBe(false)
  })
})

// ============================================================================
// wasCompletionAttempted
// ============================================================================

describe('wasCompletionAttempted', () => {
  it('should return false for unknown session', () => {
    expect(wasCompletionAttempted('nonexistent-session')).toBe(false)
  })

  it('should return true after completion tool executes', async () => {
    const ctx = createTestContext()
    await completionTool.execute({ result: 'Task done' }, ctx)
    expect(wasCompletionAttempted('test-session')).toBe(true)
  })

  it('should return false after state is reset', async () => {
    const ctx = createTestContext()
    await completionTool.execute({ result: 'Task done' }, ctx)
    resetCompletionState('test-session')
    expect(wasCompletionAttempted('test-session')).toBe(false)
  })
})

// ============================================================================
// getCompletionDetails
// ============================================================================

describe('getCompletionDetails', () => {
  it('should return null for unknown session', () => {
    expect(getCompletionDetails('nonexistent-session')).toBeNull()
  })

  it('should return result after completion', async () => {
    const ctx = createTestContext()
    await completionTool.execute({ result: 'Created 3 components' }, ctx)

    const details = getCompletionDetails('test-session')
    expect(details).not.toBeNull()
    expect(details!.result).toBe('Created 3 components')
    expect(details!.command).toBeUndefined()
  })

  it('should return result and command', async () => {
    const ctx = createTestContext()
    await completionTool.execute({ result: 'Server ready', command: 'npm run dev' }, ctx)

    const details = getCompletionDetails('test-session')
    expect(details).not.toBeNull()
    expect(details!.result).toBe('Server ready')
    expect(details!.command).toBe('npm run dev')
  })

  it('should return null after state is reset', async () => {
    const ctx = createTestContext()
    await completionTool.execute({ result: 'Done' }, ctx)
    resetCompletionState('test-session')

    expect(getCompletionDetails('test-session')).toBeNull()
  })
})

// ============================================================================
// resetCompletionState
// ============================================================================

describe('resetCompletionState', () => {
  it('should not throw for unknown session', () => {
    expect(() => resetCompletionState('nonexistent')).not.toThrow()
  })

  it('should clear existing state', async () => {
    const ctx = createTestContext()
    await completionTool.execute({ result: 'Done' }, ctx)
    expect(wasCompletionAttempted('test-session')).toBe(true)

    resetCompletionState('test-session')
    expect(wasCompletionAttempted('test-session')).toBe(false)
    expect(getCompletionDetails('test-session')).toBeNull()
  })
})

// ============================================================================
// cleanupCompletionStates
// ============================================================================

describe('cleanupCompletionStates', () => {
  it('should not throw when no states exist', () => {
    expect(() => cleanupCompletionStates()).not.toThrow()
  })

  it('should remove entries older than 1 hour', async () => {
    // Create a completion state
    const ctx = createTestContext('old-session')
    await completionTool.execute({ result: 'Old task' }, ctx)

    // Mock Date.now to return future time (>1 hour ahead)
    const originalDateNow = Date.now
    Date.now = () => originalDateNow() + 61 * 60 * 1000

    cleanupCompletionStates()

    // Restore Date.now
    Date.now = originalDateNow

    expect(wasCompletionAttempted('old-session')).toBe(false)
  })

  it('should keep entries newer than 1 hour', async () => {
    const ctx = createTestContext('recent-session')
    await completionTool.execute({ result: 'Recent task' }, ctx)

    cleanupCompletionStates()

    expect(wasCompletionAttempted('recent-session')).toBe(true)
  })
})

// ============================================================================
// formatCompletionResult
// ============================================================================

describe('formatCompletionResult', () => {
  it('should format result only', () => {
    const formatted = formatCompletionResult('Task completed successfully')
    expect(formatted).toBe('## Task Completed\n\nTask completed successfully')
  })

  it('should format result with command', () => {
    const formatted = formatCompletionResult('Server is ready', 'npm run dev')
    expect(formatted).toBe(
      '## Task Completed\n\nServer is ready\n\n### Try it out\n```bash\nnpm run dev\n```'
    )
  })

  it('should handle multiline result', () => {
    const result = 'Line 1\nLine 2\nLine 3'
    const formatted = formatCompletionResult(result)
    expect(formatted).toContain('Line 1\nLine 2\nLine 3')
    expect(formatted.startsWith('## Task Completed\n\n')).toBe(true)
  })

  it('should not include command section when command is undefined', () => {
    const formatted = formatCompletionResult('Done')
    expect(formatted).not.toContain('### Try it out')
    expect(formatted).not.toContain('```bash')
  })

  it('should include bash code block for command', () => {
    const formatted = formatCompletionResult('Done', 'python main.py')
    expect(formatted).toContain('```bash\npython main.py\n```')
  })
})

// ============================================================================
// completionTool definition
// ============================================================================

describe('completionTool', () => {
  it('should have correct name', () => {
    expect(completionTool.definition.name).toBe('attempt_completion')
  })

  it('should have a description', () => {
    expect(completionTool.definition.description).toBeTruthy()
    expect(typeof completionTool.definition.description).toBe('string')
  })

  it('should have input_schema', () => {
    expect(completionTool.definition.input_schema).toBeDefined()
    expect(completionTool.definition.input_schema.type).toBe('object')
  })

  it('should execute successfully with valid params', async () => {
    const ctx = createTestContext()
    const result = await completionTool.execute({ result: 'All done' }, ctx)

    expect(result.success).toBe(true)
    expect(result.output).toContain('Task completion signaled')
    expect(result.output).toContain('All done')
  })

  it('should include command in output when provided', async () => {
    const ctx = createTestContext()
    const result = await completionTool.execute({ result: 'Ready', command: 'npm test' }, ctx)

    expect(result.success).toBe(true)
    expect(result.output).toContain('npm test')
    expect(result.output).toContain('Demo command')
  })

  it('should set metadata with completionAttempted', async () => {
    const ctx = createTestContext()
    const result = await completionTool.execute({ result: 'Done' }, ctx)

    expect(result.metadata).toBeDefined()
    expect(result.metadata!.completionAttempted).toBe(true)
  })

  it('should set hasCommand metadata correctly', async () => {
    const ctx = createTestContext()

    const r1 = await completionTool.execute({ result: 'Done' }, ctx)
    expect(r1.metadata!.hasCommand).toBe(false)

    resetCompletionState('test-session')

    const r2 = await completionTool.execute({ result: 'Done', command: 'ls' }, ctx)
    expect(r2.metadata!.hasCommand).toBe(true)
  })

  it('should track state for different sessions independently', async () => {
    const ctx1 = createTestContext('session-1')
    const ctx2 = createTestContext('session-2')

    await completionTool.execute({ result: 'Task 1 done' }, ctx1)

    expect(wasCompletionAttempted('session-1')).toBe(true)
    expect(wasCompletionAttempted('session-2')).toBe(false)

    await completionTool.execute({ result: 'Task 2 done' }, ctx2)

    expect(wasCompletionAttempted('session-1')).toBe(true)
    expect(wasCompletionAttempted('session-2')).toBe(true)
  })
})
