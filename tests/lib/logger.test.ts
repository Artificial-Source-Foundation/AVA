/**
 * Tests for Delta9 Structured Logger
 *
 * Note: The logger uses process.stderr.write() and requires DELTA9_DEBUG=1
 * to enable output (to avoid corrupting the TUI in production).
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
  createLogger,
  initLogger,
  getLogger,
  getNamedLogger,
  debug,
  info,
  warn,
  error,
} from '../../src/lib/logger.js'

describe('Logger', () => {
  let stderrSpy: ReturnType<typeof vi.spyOn>
  const originalDebugEnv = process.env.DELTA9_DEBUG

  beforeEach(() => {
    // Enable debug mode for tests
    process.env.DELTA9_DEBUG = '1'
    // Spy on process.stderr.write (the logger uses this, not console)
    stderrSpy = vi.spyOn(process.stderr, 'write').mockImplementation(() => true)
  })

  afterEach(() => {
    vi.restoreAllMocks()
    // Restore original env
    if (originalDebugEnv !== undefined) {
      process.env.DELTA9_DEBUG = originalDebugEnv
    } else {
      delete process.env.DELTA9_DEBUG
    }
  })

  describe('createLogger', () => {
    it('creates a console logger without client', () => {
      const logger = createLogger()
      expect(logger).toBeDefined()
      expect(logger.debug).toBeTypeOf('function')
      expect(logger.info).toBeTypeOf('function')
      expect(logger.warn).toBeTypeOf('function')
      expect(logger.error).toBeTypeOf('function')
      expect(logger.child).toBeTypeOf('function')
    })

    it('logs messages at each level', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.debug('debug message')
      logger.info('info message')
      logger.warn('warn message')
      logger.error('error message')

      // Each log level writes to stderr
      expect(stderrSpy).toHaveBeenCalledTimes(4)
    })

    it('respects minimum log level', () => {
      const logger = createLogger(undefined, {}, 'warn')

      logger.debug('debug')
      logger.info('info')
      logger.warn('warn')
      logger.error('error')

      // Only warn and error should be logged
      expect(stderrSpy).toHaveBeenCalledTimes(2)
    })

    it('includes context in log output', () => {
      const logger = createLogger(undefined, { component: 'test' }, 'debug')

      logger.info('test message')

      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[delta9:test]'))
    })

    it('includes data in log output', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.info('test message', { foo: 'bar', num: 42 })

      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('foo=bar'))
      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('num=42'))
    })

    it('does not log when DELTA9_DEBUG is not set', () => {
      delete process.env.DELTA9_DEBUG

      const logger = createLogger(undefined, {}, 'debug')
      logger.info('should not appear')

      expect(stderrSpy).not.toHaveBeenCalled()
    })
  })

  describe('child logger', () => {
    it('creates child logger with merged context', () => {
      const parent = createLogger(undefined, { component: 'parent' }, 'debug')
      const child = parent.child({ taskId: 'task_123' })

      child.info('child message')

      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[delta9:parent]'))
      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('taskId=task_123'))
    })

    it('child context overrides parent context', () => {
      const parent = createLogger(undefined, { component: 'parent' }, 'debug')
      const child = parent.child({ component: 'child' })

      child.info('child message')

      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[delta9:child]'))
    })
  })

  describe('initLogger', () => {
    it('initializes default logger', () => {
      initLogger(undefined, 'debug')
      const logger = getLogger()

      logger.info('test')

      expect(stderrSpy).toHaveBeenCalled()
    })
  })

  describe('getNamedLogger', () => {
    it('creates logger with component name', () => {
      initLogger(undefined, 'debug')
      const logger = getNamedLogger('background')

      logger.info('test message')

      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[delta9:background]'))
    })
  })

  describe('convenience functions', () => {
    beforeEach(() => {
      initLogger(undefined, 'debug')
    })

    it('debug logs to stderr', () => {
      debug('debug message')
      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[DEBUG]'))
    })

    it('info logs to stderr', () => {
      info('info message')
      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[INFO]'))
    })

    it('warn logs to stderr', () => {
      warn('warn message')
      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[WARN]'))
    })

    it('error logs to stderr', () => {
      error('error message')
      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[ERROR]'))
    })
  })

  describe('message formatting', () => {
    it('includes timestamp in HH:MM:SS.mmm format', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.info('test')

      // Check for timestamp pattern
      expect(stderrSpy).toHaveBeenCalledWith(expect.stringMatching(/\d{2}:\d{2}:\d{2}\.\d{3}/))
    })

    it('includes log level in uppercase', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.info('test')

      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('[INFO]'))
    })

    it('handles object data serialization', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.info('test', { nested: { key: 'value' } })

      expect(stderrSpy).toHaveBeenCalledWith(expect.stringContaining('nested={"key":"value"}'))
    })
  })
})
