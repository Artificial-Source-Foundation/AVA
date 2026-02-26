import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { AgentEvent } from '../agent/types.js'
import { AvaLogger, getLogger, resetLogger, setLogger } from './logger.js'
import type { LogEntry } from './types.js'

describe('AvaLogger', () => {
  let logger: AvaLogger

  beforeEach(() => {
    logger = new AvaLogger({ file: false, stderr: false })
  })

  // ==========================================================================
  // Basic Logging
  // ==========================================================================

  describe('log levels', () => {
    it('should emit entries at or above configured level', () => {
      const entries: LogEntry[] = []
      logger = new AvaLogger({
        file: false,
        stderr: false,
        level: 'warn',
        callback: (entry) => entries.push(entry),
      })

      logger.debug('debug msg')
      logger.info('info msg')
      logger.warn('warn msg')
      logger.error('error msg')

      expect(entries).toHaveLength(2)
      expect(entries[0].level).toBe('warn')
      expect(entries[1].level).toBe('error')
    })

    it('should emit all levels when level is debug', () => {
      const entries: LogEntry[] = []
      logger = new AvaLogger({
        file: false,
        stderr: false,
        level: 'debug',
        callback: (entry) => entries.push(entry),
      })

      logger.debug('d')
      logger.info('i')
      logger.warn('w')
      logger.error('e')

      expect(entries).toHaveLength(4)
    })

    it('should include message and data in log entries', () => {
      const entries: LogEntry[] = []
      logger = new AvaLogger({
        file: false,
        stderr: false,
        level: 'debug',
        callback: (entry) => entries.push(entry),
      })

      logger.info('test message', { key: 'value' })

      expect(entries[0].message).toBe('test message')
      expect(entries[0].data).toEqual({ key: 'value' })
      expect(entries[0].timestamp).toBeTruthy()
    })
  })

  // ==========================================================================
  // Stderr Output
  // ==========================================================================

  describe('stderr output', () => {
    it('should write to stderr when enabled', () => {
      const stderrSpy = vi.spyOn(process.stderr, 'write').mockReturnValue(true)

      logger = new AvaLogger({ file: false, stderr: true, level: 'info' })
      logger.info('hello stderr')

      expect(stderrSpy).toHaveBeenCalledWith('[INFO] hello stderr\n')
      stderrSpy.mockRestore()
    })

    it('should not write to stderr when disabled', () => {
      const stderrSpy = vi.spyOn(process.stderr, 'write').mockReturnValue(true)

      logger = new AvaLogger({ file: false, stderr: false })
      logger.info('should not appear')

      expect(stderrSpy).not.toHaveBeenCalled()
      stderrSpy.mockRestore()
    })
  })

  // ==========================================================================
  // Custom Callback
  // ==========================================================================

  describe('custom callback', () => {
    it('should call callback for each emitted entry', () => {
      const callback = vi.fn()
      logger = new AvaLogger({ file: false, stderr: false, callback })

      logger.info('first')
      logger.warn('second')

      expect(callback).toHaveBeenCalledTimes(2)
      expect(callback.mock.calls[0][0].message).toBe('first')
      expect(callback.mock.calls[1][0].message).toBe('second')
    })

    it('should not crash if callback throws', () => {
      const callback = vi.fn().mockImplementation(() => {
        throw new Error('callback error')
      })
      logger = new AvaLogger({ file: false, stderr: false, callback })

      expect(() => logger.info('should not crash')).not.toThrow()
    })
  })

  // ==========================================================================
  // Agent Event Mapping
  // ==========================================================================

  describe('fromAgentEvent', () => {
    it('should map agent:start event', () => {
      const entries: LogEntry[] = []
      logger = new AvaLogger({
        file: false,
        stderr: false,
        level: 'debug',
        callback: (e) => entries.push(e),
      })

      const event: AgentEvent = {
        type: 'agent:start',
        agentId: 'test-agent',
        timestamp: Date.now(),
        goal: 'test goal',
        config: { maxTimeMinutes: 10, maxTurns: 20, maxRetries: 3, gracePeriodMs: 60000 },
      }

      logger.fromAgentEvent(event)

      expect(entries).toHaveLength(1)
      expect(entries[0].level).toBe('info')
      expect(entries[0].message).toContain('Agent started')
      expect(entries[0].message).toContain('test goal')
      expect(entries[0].agentEventType).toBe('agent:start')
      expect(entries[0].agentId).toBe('test-agent')
    })

    it('should map tool:error event as error level', () => {
      const entries: LogEntry[] = []
      logger = new AvaLogger({
        file: false,
        stderr: false,
        level: 'debug',
        callback: (e) => entries.push(e),
      })

      const event: AgentEvent = {
        type: 'tool:error',
        agentId: 'test-agent',
        timestamp: Date.now(),
        toolName: 'read_file',
        error: 'File not found',
      }

      logger.fromAgentEvent(event)

      expect(entries[0].level).toBe('error')
      expect(entries[0].message).toContain('read_file')
      expect(entries[0].message).toContain('File not found')
    })

    it('should map all 16 agent event types', () => {
      const entries: LogEntry[] = []
      logger = new AvaLogger({
        file: false,
        stderr: false,
        level: 'debug',
        callback: (e) => entries.push(e),
      })

      const events: AgentEvent[] = [
        {
          type: 'agent:start',
          agentId: 'a',
          timestamp: 1,
          goal: 'g',
          config: { maxTimeMinutes: 10, maxTurns: 20, maxRetries: 3, gracePeriodMs: 60000 },
        },
        {
          type: 'agent:finish',
          agentId: 'a',
          timestamp: 2,
          result: {
            success: true,
            terminateMode: 'GOAL' as const,
            output: '',
            steps: [],
            tokensUsed: 0,
            durationMs: 0,
            turns: 0,
          },
        },
        { type: 'turn:start', agentId: 'a', timestamp: 3, turn: 0 },
        { type: 'turn:finish', agentId: 'a', timestamp: 4, turn: 0, toolCalls: [] },
        {
          type: 'tool:start',
          agentId: 'a',
          timestamp: 5,
          toolName: 'read_file',
          args: {},
        },
        {
          type: 'tool:finish',
          agentId: 'a',
          timestamp: 6,
          toolName: 'read_file',
          success: true,
          output: '',
          durationMs: 10,
        },
        {
          type: 'tool:error',
          agentId: 'a',
          timestamp: 7,
          toolName: 'read_file',
          error: 'err',
        },
        {
          type: 'tool:metadata',
          agentId: 'a',
          timestamp: 8,
          toolName: 'bash',
          metadata: {},
        },
        { type: 'thought', agentId: 'a', timestamp: 9, text: 'thinking...' },
        {
          type: 'recovery:start',
          agentId: 'a',
          timestamp: 10,
          reason: 'TIMEOUT' as const,
          turn: 0,
        },
        {
          type: 'recovery:finish',
          agentId: 'a',
          timestamp: 11,
          success: false,
          durationMs: 100,
        },
        {
          type: 'validation:start',
          agentId: 'a',
          timestamp: 12,
          files: ['a.ts'],
        },
        {
          type: 'validation:result',
          agentId: 'a',
          timestamp: 13,
          passed: true,
          summary: 'ok',
        },
        {
          type: 'validation:finish',
          agentId: 'a',
          timestamp: 14,
          passed: true,
          durationMs: 50,
        },
        {
          type: 'provider:switch',
          agentId: 'a',
          timestamp: 15,
          provider: 'openai',
          model: 'gpt-4',
        },
        { type: 'error', agentId: 'a', timestamp: 16, error: 'something broke' },
      ]

      for (const event of events) {
        logger.fromAgentEvent(event)
      }

      expect(entries).toHaveLength(16)
      // Each entry has the correct agentEventType
      for (let i = 0; i < events.length; i++) {
        expect(entries[i].agentEventType).toBe(events[i].type)
      }
    })
  })

  // ==========================================================================
  // Configure
  // ==========================================================================

  describe('configure', () => {
    it('should update configuration', () => {
      logger = new AvaLogger({ file: false, stderr: false, level: 'info' })
      expect(logger.getConfig().level).toBe('info')

      logger.configure({ level: 'error' })
      expect(logger.getConfig().level).toBe('error')
    })
  })
})

// ============================================================================
// Singleton
// ============================================================================

describe('Logger singleton', () => {
  afterEach(() => {
    resetLogger()
  })

  it('should return the same instance', () => {
    const a = getLogger()
    const b = getLogger()
    expect(a).toBe(b)
  })

  it('should allow setting a custom instance', () => {
    const custom = new AvaLogger({ file: false, stderr: false })
    setLogger(custom)
    expect(getLogger()).toBe(custom)
  })

  it('should reset to a new instance', () => {
    const first = getLogger()
    resetLogger()
    const second = getLogger()
    expect(first).not.toBe(second)
  })
})
