import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { configureLogger, createLogger, getLoggerConfig, resetLogger } from './logger.js'
import type { LogEntry, SimpleLogger } from './types.js'

describe('Logger', () => {
  let entries: LogEntry[]
  let log: SimpleLogger

  beforeEach(() => {
    entries = []
    resetLogger()
    configureLogger({
      level: 'debug',
      stderr: false,
      callback: (entry) => entries.push(entry),
    })
    log = createLogger('Test')
  })

  afterEach(() => {
    resetLogger()
  })

  // ─── Basic Logging ──────────────────────────────────────────────────────

  describe('basic logging', () => {
    it('logs debug messages', () => {
      log.debug('hello')
      expect(entries).toHaveLength(1)
      expect(entries[0].level).toBe('debug')
      expect(entries[0].message).toBe('hello')
      expect(entries[0].source).toBe('Test')
    })

    it('logs info messages', () => {
      log.info('info msg')
      expect(entries).toHaveLength(1)
      expect(entries[0].level).toBe('info')
    })

    it('logs warn messages', () => {
      log.warn('warn msg')
      expect(entries).toHaveLength(1)
      expect(entries[0].level).toBe('warn')
    })

    it('logs error messages', () => {
      log.error('error msg')
      expect(entries).toHaveLength(1)
      expect(entries[0].level).toBe('error')
    })

    it('includes data in log entry', () => {
      log.info('with data', { key: 'value' })
      expect(entries[0].data).toEqual({ key: 'value' })
    })

    it('includes timestamp in log entry', () => {
      log.info('test')
      expect(entries[0].timestamp).toBeTruthy()
      expect(() => new Date(entries[0].timestamp)).not.toThrow()
    })
  })

  // ─── Level Filtering ───────────────────────────────────────────────────

  describe('level filtering', () => {
    it('filters debug when level is info', () => {
      configureLogger({ level: 'info' })
      log.debug('should be filtered')
      expect(entries).toHaveLength(0)
    })

    it('passes info when level is info', () => {
      configureLogger({ level: 'info' })
      log.info('should pass')
      expect(entries).toHaveLength(1)
    })

    it('filters debug and info when level is warn', () => {
      configureLogger({ level: 'warn' })
      log.debug('no')
      log.info('no')
      log.warn('yes')
      log.error('yes')
      expect(entries).toHaveLength(2)
    })

    it('only passes error when level is error', () => {
      configureLogger({ level: 'error' })
      log.debug('no')
      log.info('no')
      log.warn('no')
      log.error('yes')
      expect(entries).toHaveLength(1)
    })
  })

  // ─── Timing ─────────────────────────────────────────────────────────────

  describe('timing', () => {
    it('logs timing with duration', () => {
      const start = Date.now() - 100
      log.timing('operation', start)
      expect(entries).toHaveLength(1)
      expect(entries[0].level).toBe('debug')
      expect(entries[0].message).toMatch(/operation completed in \d+ms/)
    })

    it('includes extra data in timing', () => {
      log.timing('op', Date.now(), { extra: 'data' })
      expect(entries[0].data).toEqual({ extra: 'data' })
    })
  })

  // ─── Child Loggers ──────────────────────────────────────────────────────

  describe('child loggers', () => {
    it('creates child with combined source', () => {
      const child = log.child('Sub')
      child.info('from child')
      expect(entries[0].source).toBe('Test:Sub')
    })

    it('creates nested children', () => {
      const grandchild = log.child('A').child('B')
      grandchild.info('nested')
      expect(entries[0].source).toBe('Test:A:B')
    })
  })

  // ─── Config ─────────────────────────────────────────────────────────────

  describe('config', () => {
    it('returns current config', () => {
      const config = getLoggerConfig()
      expect(config.level).toBe('debug')
    })

    it('resets to default config', () => {
      configureLogger({ level: 'error' })
      resetLogger()
      const config = getLoggerConfig()
      expect(config.level).toBe('info')
    })

    it('merges partial config', () => {
      configureLogger({ level: 'error' })
      const config = getLoggerConfig()
      expect(config.level).toBe('error')
      expect(config.stderr).toBe(false)
    })
  })

  // ─── stderr output ─────────────────────────────────────────────────────

  describe('stderr', () => {
    it('writes to stderr when enabled', () => {
      const spy = vi.spyOn(process.stderr, 'write').mockImplementation(() => true)
      configureLogger({ stderr: true, level: 'debug' })
      log.info('stderr test')
      expect(spy).toHaveBeenCalledWith(expect.stringContaining('stderr test'))
      spy.mockRestore()
    })
  })
})
