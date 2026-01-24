/**
 * Tests for Delta9 Rich Error Handling
 */

import { describe, it, expect } from 'vitest'
import {
  Delta9Error,
  errors,
  isDelta9Error,
  formatErrorResponse,
} from '../../src/lib/errors.js'

describe('Delta9Error', () => {
  describe('constructor', () => {
    it('creates error with required fields', () => {
      const err = new Delta9Error({
        code: 'TEST_ERROR',
        message: 'Test error message',
      })

      expect(err).toBeInstanceOf(Error)
      expect(err).toBeInstanceOf(Delta9Error)
      expect(err.name).toBe('Delta9Error')
      expect(err.code).toBe('TEST_ERROR')
      expect(err.message).toBe('Test error message')
      expect(err.suggestions).toEqual([])
    })

    it('creates error with suggestions', () => {
      const err = new Delta9Error({
        code: 'TEST_ERROR',
        message: 'Test error',
        suggestions: ['Try this', 'Or try that'],
      })

      expect(err.suggestions).toEqual(['Try this', 'Or try that'])
    })

    it('creates error with context', () => {
      const err = new Delta9Error({
        code: 'TEST_ERROR',
        message: 'Test error',
        context: { taskId: 'task_123', attempt: 2 },
      })

      expect(err.context).toEqual({ taskId: 'task_123', attempt: 2 })
    })

    it('creates error with cause', () => {
      const cause = new Error('Original error')
      const err = new Delta9Error({
        code: 'TEST_ERROR',
        message: 'Wrapped error',
        cause,
      })

      expect(err.cause).toBe(cause)
    })
  })

  describe('toJSON', () => {
    it('serializes error to JSON object', () => {
      const err = new Delta9Error({
        code: 'TEST_ERROR',
        message: 'Test message',
        suggestions: ['Suggestion 1'],
        context: { key: 'value' },
      })

      const json = err.toJSON()

      expect(json).toEqual({
        success: false,
        error: {
          code: 'TEST_ERROR',
          message: 'Test message',
          suggestions: ['Suggestion 1'],
          context: { key: 'value' },
        },
      })
    })

    it('returns undefined suggestions when array is empty', () => {
      const err = new Delta9Error({
        code: 'TEST_ERROR',
        message: 'Test message',
      })

      const json = err.toJSON() as { error: { suggestions?: string[] } }

      // Empty suggestions array results in undefined in JSON output
      expect(json.error.suggestions).toBeUndefined()
    })
  })

  describe('toToolResponse', () => {
    it('returns JSON string', () => {
      const err = new Delta9Error({
        code: 'TEST_ERROR',
        message: 'Test message',
      })

      const response = err.toToolResponse()

      expect(typeof response).toBe('string')
      expect(JSON.parse(response)).toEqual(err.toJSON())
    })
  })
})

describe('Error factories', () => {
  describe('taskNotFound', () => {
    it('creates task not found error', () => {
      const err = errors.taskNotFound('bg_123')

      expect(err.code).toBe('TASK_NOT_FOUND')
      expect(err.message).toContain('bg_123')
      expect(err.suggestions.length).toBeGreaterThan(0)
      expect(err.context).toEqual({ taskId: 'bg_123' })
    })
  })

  describe('taskAlreadyComplete', () => {
    it('creates task already complete error', () => {
      const err = errors.taskAlreadyComplete('bg_123', 'completed')

      expect(err.code).toBe('TASK_ALREADY_COMPLETE')
      expect(err.message).toContain('bg_123')
      expect(err.message).toContain('completed')
      expect(err.context).toEqual({ taskId: 'bg_123', status: 'completed' })
    })
  })

  describe('taskCancelFailed', () => {
    it('creates task cancel failed error', () => {
      const err = errors.taskCancelFailed('bg_123', 'completed')

      expect(err.code).toBe('TASK_CANCEL_FAILED')
      expect(err.message).toContain('bg_123')
      expect(err.suggestions).toContain('Only pending or running tasks can be cancelled')
    })
  })

  describe('taskTimeout', () => {
    it('creates task timeout error with formatted time', () => {
      const err = errors.taskTimeout('bg_123', 30000)

      expect(err.code).toBe('TASK_TIMEOUT')
      expect(err.message).toContain('30s')
      expect(err.context).toEqual({ taskId: 'bg_123', timeoutMs: 30000 })
    })
  })

  describe('noActiveMission', () => {
    it('creates no active mission error', () => {
      const err = errors.noActiveMission()

      expect(err.code).toBe('NO_ACTIVE_MISSION')
      expect(err.suggestions).toContain('Create a mission first with mission_create')
    })
  })

  describe('missionAlreadyExists', () => {
    it('creates mission already exists error', () => {
      const err = errors.missionAlreadyExists('mission_abc')

      expect(err.code).toBe('MISSION_ALREADY_EXISTS')
      expect(err.message).toContain('mission_abc')
    })
  })

  describe('missionTaskNotFound', () => {
    it('creates mission task not found error', () => {
      const err = errors.missionTaskNotFound('task_123')

      expect(err.code).toBe('MISSION_TASK_NOT_FOUND')
      expect(err.suggestions).toContain('Use mission_status to see all tasks')
    })
  })

  describe('sdkUnavailable', () => {
    it('creates SDK unavailable error', () => {
      const err = errors.sdkUnavailable()

      expect(err.code).toBe('SDK_UNAVAILABLE')
      expect(err.suggestions).toContain('Running in simulation mode - background tasks will be simulated')
    })
  })

  describe('sessionCreateFailed', () => {
    it('creates session create failed error with cause', () => {
      const cause = new Error('Connection refused')
      const err = errors.sessionCreateFailed(cause)

      expect(err.code).toBe('SESSION_CREATE_FAILED')
      expect(err.message).toContain('Connection refused')
      expect(err.cause).toBe(cause)
    })
  })

  describe('councilNotConfigured', () => {
    it('creates council not configured error', () => {
      const err = errors.councilNotConfigured()

      expect(err.code).toBe('COUNCIL_NOT_CONFIGURED')
      expect(err.suggestions.some(s => s.includes('delta9.json'))).toBe(true)
    })
  })

  describe('configInvalid', () => {
    it('creates config invalid error with validation errors', () => {
      const err = errors.configInvalid('/path/config.json', [
        'Missing field: council',
        'Invalid type: budget',
      ])

      expect(err.code).toBe('CONFIG_INVALID')
      expect(err.suggestions).toContain('Fix: Missing field: council')
      expect(err.suggestions).toContain('Fix: Invalid type: budget')
    })
  })

  describe('validationFailed', () => {
    it('creates validation failed error with failures', () => {
      const err = errors.validationFailed('task_123', [
        'Tests not passing',
        'Missing documentation',
      ])

      expect(err.code).toBe('VALIDATION_FAILED')
      expect(err.suggestions).toContain('Address: Tests not passing')
      expect(err.suggestions).toContain('Use retry_task to attempt again after fixes')
    })
  })

  describe('memoryKeyNotFound', () => {
    it('creates memory key not found error', () => {
      const err = errors.memoryKeyNotFound('my_key')

      expect(err.code).toBe('MEMORY_KEY_NOT_FOUND')
      expect(err.message).toContain('my_key')
      expect(err.suggestions).toContain('Keys are case-sensitive')
    })
  })
})

describe('isDelta9Error', () => {
  it('returns true for Delta9Error', () => {
    const err = new Delta9Error({ code: 'TEST', message: 'test' })
    expect(isDelta9Error(err)).toBe(true)
  })

  it('returns false for regular Error', () => {
    const err = new Error('test')
    expect(isDelta9Error(err)).toBe(false)
  })

  it('returns false for non-errors', () => {
    expect(isDelta9Error('string')).toBe(false)
    expect(isDelta9Error(null)).toBe(false)
    expect(isDelta9Error(undefined)).toBe(false)
    expect(isDelta9Error({ code: 'FAKE' })).toBe(false)
  })
})

describe('formatErrorResponse', () => {
  it('formats Delta9Error', () => {
    const err = errors.taskNotFound('bg_123')
    const response = formatErrorResponse(err)

    const parsed = JSON.parse(response)
    expect(parsed.success).toBe(false)
    expect(parsed.error.code).toBe('TASK_NOT_FOUND')
  })

  it('formats regular Error', () => {
    const err = new Error('Something went wrong')
    const response = formatErrorResponse(err)

    const parsed = JSON.parse(response)
    expect(parsed.success).toBe(false)
    expect(parsed.error.code).toBe('UNKNOWN_ERROR')
    expect(parsed.error.message).toBe('Something went wrong')
  })

  it('formats unknown error types', () => {
    const response = formatErrorResponse('string error')

    const parsed = JSON.parse(response)
    expect(parsed.success).toBe(false)
    expect(parsed.error.code).toBe('UNKNOWN_ERROR')
    expect(parsed.error.message).toBe('string error')
  })
})
