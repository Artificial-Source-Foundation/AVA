import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { createLogger } from '@ava/core-v2/logger'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { createMockPlatform } from '../../../core-v2/src/__test-utils__/mock-platform.js'
import { createErrorRecoveryMiddleware } from './error-recovery-middleware.js'

const { dispatchComputeMock } = vi.hoisted(() => ({ dispatchComputeMock: vi.fn() }))

vi.mock('@ava/core-v2', () => ({
  dispatchCompute: dispatchComputeMock,
}))

function makeCtx(): ToolMiddlewareContext {
  return {
    toolName: 'edit',
    args: {
      filePath: '/workspace/src/app.ts',
      oldString: 'hello world',
      newString: 'hello ava',
    },
    ctx: {
      sessionId: 's1',
      workingDirectory: '/workspace',
      signal: new AbortController().signal,
    },
    definition: {
      name: 'edit',
      description: 'Edit file content',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

describe('createErrorRecoveryMiddleware', () => {
  beforeEach(() => {
    dispatchComputeMock.mockReset()
  })

  it('has priority 15', () => {
    const middleware = createErrorRecoveryMiddleware(createMockPlatform(), createLogger('test'))
    expect(middleware.priority).toBe(15)
  })

  it('does not retry fatal failures', async () => {
    const middleware = createErrorRecoveryMiddleware(createMockPlatform(), createLogger('test'))
    const result = await middleware.after?.(makeCtx(), {
      success: false,
      output: 'No space left on device',
      error: 'No space left on device',
    })

    expect(result?.result?.success).toBe(false)
    expect(
      (result?.result?.metadata as { recovery: { classification: string } }).recovery.classification
    ).toBe('fatal')
    expect(dispatchComputeMock).not.toHaveBeenCalled()
  })

  it('retries edit recovery up to three attempts and succeeds', async () => {
    const platform = createMockPlatform()
    await platform.fs.mkdir('/workspace/src')
    await platform.fs.writeFile('/workspace/src/app.ts', 'hello world')

    let fuzzyAttempts = 0
    dispatchComputeMock.mockImplementation(
      async (command: string, args: unknown, fallback: () => Promise<unknown>) => {
        if (command === 'compute_fuzzy_replace') {
          fuzzyAttempts += 1
          if (fuzzyAttempts < 3) throw new Error('replace failed')
          return 'hello ava'
        }
        if (command === 'validation_validate_edit') {
          return { valid: true }
        }
        return fallback()
      }
    )

    const middleware = createErrorRecoveryMiddleware(platform, createLogger('test'))
    const after = await middleware.after?.(makeCtx(), {
      success: false,
      output: 'edit failed',
      error: 'edit failed',
    })

    expect(after?.result?.success).toBe(true)
    expect(await platform.fs.readFile('/workspace/src/app.ts')).toBe('hello ava')
    expect(fuzzyAttempts).toBe(3)
  })

  it('marks recovery as exhausted after max retries', async () => {
    const platform = createMockPlatform()
    await platform.fs.mkdir('/workspace/src')
    await platform.fs.writeFile('/workspace/src/app.ts', 'hello world')

    dispatchComputeMock.mockImplementation(async (command: string) => {
      if (command === 'compute_fuzzy_replace') {
        throw new Error('replace failed')
      }
      return { valid: true }
    })

    const middleware = createErrorRecoveryMiddleware(platform, createLogger('test'))
    const after = await middleware.after?.(makeCtx(), {
      success: false,
      output: 'edit failed',
      error: 'edit failed',
    })

    expect(after?.result?.success).toBe(false)
    expect(
      (after?.result?.metadata as { recovery: { attempted: number; recovered: boolean } }).recovery
    ).toEqual({ attempted: 3, recovered: false, classification: 'recoverable' })
  })
})
