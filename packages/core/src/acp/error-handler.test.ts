/**
 * ACP Error Handler Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import { AcpErrorHandler, createAcpErrorHandler } from './error-handler.js'
import type { AcpSessionStore } from './session-store.js'
import { AcpError, AcpErrorCode } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function mockSessionStore(): AcpSessionStore {
  return {
    saveAll: vi.fn(async () => undefined),
    save: vi.fn(async () => undefined),
  } as unknown as AcpSessionStore
}

// ============================================================================
// Tests
// ============================================================================

describe('AcpErrorHandler', () => {
  let handler: AcpErrorHandler

  beforeEach(() => {
    handler = new AcpErrorHandler()
  })

  describe('formatError', () => {
    it('should format AcpError', () => {
      const error = new AcpError(AcpErrorCode.SESSION_NOT_FOUND, 'Not found', { id: '123' })
      const formatted = handler.formatError(error)

      expect(formatted.code).toBe(AcpErrorCode.SESSION_NOT_FOUND)
      expect(formatted.message).toBe('Not found')
      expect(formatted.data).toEqual({ id: '123' })
    })

    it('should format AbortError', () => {
      const error = new Error('cancelled')
      error.name = 'AbortError'

      const formatted = handler.formatError(error)
      expect(formatted.code).toBe(AcpErrorCode.CANCELLED)
    })

    it('should format session not found errors', () => {
      const error = new Error('Session not found: abc')
      const formatted = handler.formatError(error)

      expect(formatted.code).toBe(AcpErrorCode.SESSION_NOT_FOUND)
    })

    it('should format generic errors', () => {
      const error = new Error('something broke')
      const formatted = handler.formatError(error)

      expect(formatted.code).toBe(AcpErrorCode.INTERNAL)
      expect(formatted.message).toBe('something broke')
    })

    it('should format non-Error values', () => {
      const formatted = handler.formatError('string error')

      expect(formatted.code).toBe(AcpErrorCode.INTERNAL)
      expect(formatted.message).toBe('string error')
    })
  })

  describe('handleError', () => {
    it('should return formatted error', async () => {
      const result = await handler.handleError(new Error('test'))

      expect(result.code).toBe(AcpErrorCode.INTERNAL)
      expect(result.message).toBe('test')
    })

    it('should track error count', async () => {
      await handler.handleError(new Error('1'))
      await handler.handleError(new Error('2'))

      expect(handler.getRecentErrorCount()).toBe(2)
    })

    it('should trigger emergency save at threshold', async () => {
      const store = mockSessionStore()
      handler.setSessionStore(store)

      // Trigger 3 errors (threshold)
      await handler.handleError(new Error('1'))
      await handler.handleError(new Error('2'))
      await handler.handleError(new Error('3'))

      expect(store.saveAll).toHaveBeenCalled()
    })
  })

  describe('handleDisconnect', () => {
    it('should save all sessions', async () => {
      const store = mockSessionStore()
      handler.setSessionStore(store)

      await handler.handleDisconnect()

      expect(store.saveAll).toHaveBeenCalled()
    })

    it('should notify disconnect callbacks', async () => {
      const callback = vi.fn()
      handler.onDisconnect(callback)

      await handler.handleDisconnect()

      expect(callback).toHaveBeenCalled()
    })

    it('should handle callback errors gracefully', async () => {
      handler.onDisconnect(() => {
        throw new Error('callback error')
      })

      await expect(handler.handleDisconnect()).resolves.toBeUndefined()
    })
  })

  describe('onDisconnect', () => {
    it('should support unsubscribe', async () => {
      const callback = vi.fn()
      const unsubscribe = handler.onDisconnect(callback)

      unsubscribe()
      await handler.handleDisconnect()

      expect(callback).not.toHaveBeenCalled()
    })
  })

  describe('isDisconnectError', () => {
    it('should detect AcpError disconnect', () => {
      const error = new AcpError(AcpErrorCode.DISCONNECTED, 'gone')
      expect(handler.isDisconnectError(error)).toBe(true)
    })

    it('should detect broken pipe', () => {
      expect(handler.isDisconnectError(new Error('Broken pipe'))).toBe(true)
    })

    it('should detect EPIPE', () => {
      expect(handler.isDisconnectError(new Error('EPIPE error'))).toBe(true)
    })

    it('should detect connection reset', () => {
      expect(handler.isDisconnectError(new Error('Connection reset by peer'))).toBe(true)
    })

    it('should detect stream closed', () => {
      expect(handler.isDisconnectError(new Error('stream closed'))).toBe(true)
    })

    it('should detect stdin closed', () => {
      expect(handler.isDisconnectError(new Error('stdin closed'))).toBe(true)
    })

    it('should return false for normal errors', () => {
      expect(handler.isDisconnectError(new Error('some other error'))).toBe(false)
    })
  })

  describe('isTransientError', () => {
    it('should detect timeout', () => {
      expect(handler.isTransientError(new Error('Request timeout'))).toBe(true)
    })

    it('should detect connection reset', () => {
      expect(handler.isTransientError(new Error('ECONNRESET'))).toBe(true)
    })

    it('should detect connection refused', () => {
      expect(handler.isTransientError(new Error('ECONNREFUSED'))).toBe(true)
    })

    it('should detect rate limit', () => {
      expect(handler.isTransientError(new Error('Rate limit exceeded'))).toBe(true)
    })

    it('should return false for permanent errors', () => {
      expect(handler.isTransientError(new Error('file not found'))).toBe(false)
    })
  })

  describe('error history', () => {
    it('should track errors', async () => {
      await handler.handleError(new Error('a'), 'context-a')
      await handler.handleError(new Error('b'), 'context-b')

      const history = handler.getErrorHistory()
      expect(history).toHaveLength(2)
      expect(history[0]!.context).toBe('context-a')
      expect(history[1]!.context).toBe('context-b')
    })

    it('should clear errors', async () => {
      await handler.handleError(new Error('x'))
      handler.clearErrors()

      expect(handler.getRecentErrorCount()).toBe(0)
      expect(handler.getErrorHistory()).toHaveLength(0)
    })
  })

  describe('dispose', () => {
    it('should save on dispose', async () => {
      const store = mockSessionStore()
      handler.setSessionStore(store)

      await handler.dispose()

      expect(store.saveAll).toHaveBeenCalled()
    })

    it('should handle double dispose', async () => {
      await handler.dispose()
      await expect(handler.dispose()).resolves.toBeUndefined()
    })
  })

  describe('factory', () => {
    it('should create handler with factory', () => {
      const h = createAcpErrorHandler()
      expect(h).toBeInstanceOf(AcpErrorHandler)
    })
  })
})
