/**
 * Tests for Delta9 Structured Logger
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
  createLogger,
  initLogger,
  getLogger,
  getNamedLogger,
  setDefaultLogger,
  debug,
  info,
  warn,
  error,
} from '../../src/lib/logger.js'

describe('Logger', () => {
  beforeEach(() => {
    vi.spyOn(console, 'debug').mockImplementation(() => {})
    vi.spyOn(console, 'info').mockImplementation(() => {})
    vi.spyOn(console, 'warn').mockImplementation(() => {})
    vi.spyOn(console, 'error').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.restoreAllMocks()
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

      expect(console.debug).toHaveBeenCalledTimes(1)
      expect(console.info).toHaveBeenCalledTimes(1)
      expect(console.warn).toHaveBeenCalledTimes(1)
      expect(console.error).toHaveBeenCalledTimes(1)
    })

    it('respects minimum log level', () => {
      const logger = createLogger(undefined, {}, 'warn')

      logger.debug('debug')
      logger.info('info')
      logger.warn('warn')
      logger.error('error')

      expect(console.debug).not.toHaveBeenCalled()
      expect(console.info).not.toHaveBeenCalled()
      expect(console.warn).toHaveBeenCalledTimes(1)
      expect(console.error).toHaveBeenCalledTimes(1)
    })

    it('includes context in log output', () => {
      const logger = createLogger(undefined, { component: 'test' }, 'debug')

      logger.info('test message')

      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('[delta9:test]')
      )
    })

    it('includes data in log output', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.info('test message', { foo: 'bar', num: 42 })

      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('foo=bar')
      )
      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('num=42')
      )
    })
  })

  describe('child logger', () => {
    it('creates child logger with merged context', () => {
      const parent = createLogger(undefined, { component: 'parent' }, 'debug')
      const child = parent.child({ taskId: 'task_123' })

      child.info('child message')

      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('[delta9:parent]')
      )
      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('taskId=task_123')
      )
    })

    it('child context overrides parent context', () => {
      const parent = createLogger(undefined, { component: 'parent' }, 'debug')
      const child = parent.child({ component: 'child' })

      child.info('child message')

      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('[delta9:child]')
      )
    })
  })

  describe('initLogger', () => {
    it('initializes default logger', () => {
      initLogger(undefined, 'debug')
      const logger = getLogger()

      logger.info('test')

      expect(console.info).toHaveBeenCalled()
    })
  })

  describe('getNamedLogger', () => {
    it('creates logger with component name', () => {
      initLogger(undefined, 'debug')
      const logger = getNamedLogger('background')

      logger.info('test message')

      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('[delta9:background]')
      )
    })
  })

  describe('convenience functions', () => {
    beforeEach(() => {
      initLogger(undefined, 'debug')
    })

    it('debug logs to console.debug', () => {
      debug('debug message')
      expect(console.debug).toHaveBeenCalled()
    })

    it('info logs to console.info', () => {
      info('info message')
      expect(console.info).toHaveBeenCalled()
    })

    it('warn logs to console.warn', () => {
      warn('warn message')
      expect(console.warn).toHaveBeenCalled()
    })

    it('error logs to console.error', () => {
      error('error message')
      expect(console.error).toHaveBeenCalled()
    })
  })

  describe('message formatting', () => {
    it('includes timestamp in HH:MM:SS.mmm format', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.info('test')

      // Check for timestamp pattern
      expect(console.info).toHaveBeenCalledWith(
        expect.stringMatching(/\d{2}:\d{2}:\d{2}\.\d{3}/)
      )
    })

    it('includes log level in uppercase', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.info('test')

      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('[INFO]')
      )
    })

    it('handles object data serialization', () => {
      const logger = createLogger(undefined, {}, 'debug')

      logger.info('test', { nested: { key: 'value' } })

      expect(console.info).toHaveBeenCalledWith(
        expect.stringContaining('nested={"key":"value"}')
      )
    })
  })
})
