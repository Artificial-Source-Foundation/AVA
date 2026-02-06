/**
 * ACP Terminal Bridge Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import { AcpTerminalBridge, createAcpTerminalBridge } from './terminal.js'
import type { AcpTerminalCapabilities, AcpTransport } from './types.js'
import { AcpErrorCode } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function mockTransport(overrides?: Partial<AcpTransport>): AcpTransport {
  return {
    request: vi.fn(async () => undefined) as AcpTransport['request'],
    notify: vi.fn(),
    ...overrides,
  }
}

function fullCapabilities(): AcpTerminalCapabilities {
  return {
    createTerminal: true,
    writeTerminal: true,
    waitForExit: true,
    killTerminal: true,
  }
}

// ============================================================================
// Tests
// ============================================================================

describe('AcpTerminalBridge', () => {
  let bridge: AcpTerminalBridge

  beforeEach(() => {
    bridge = new AcpTerminalBridge()
  })

  describe('isAvailable', () => {
    it('should return false without transport', () => {
      expect(bridge.isAvailable()).toBe(false)
    })

    it('should return false without capabilities', () => {
      bridge.setTransport(mockTransport())
      expect(bridge.isAvailable()).toBe(false)
    })

    it('should return false with partial capabilities', () => {
      bridge.setTransport(mockTransport())
      bridge.setCapabilities({
        createTerminal: true,
        writeTerminal: false,
        waitForExit: false,
        killTerminal: false,
      })
      expect(bridge.isAvailable()).toBe(false)
    })

    it('should return true with transport and capabilities', () => {
      bridge.setTransport(mockTransport())
      bridge.setCapabilities(fullCapabilities())
      expect(bridge.isAvailable()).toBe(true)
    })
  })

  describe('execute', () => {
    it('should throw when terminal is unavailable', async () => {
      try {
        await bridge.execute('ls', '/tmp')
        expect.unreachable('Should have thrown')
      } catch (error) {
        expect((error as { code: number }).code).toBe(AcpErrorCode.TERMINAL_UNAVAILABLE)
      }
    })

    it('should execute command through editor terminal', async () => {
      const transport = mockTransport({
        request: vi.fn(async (method: string) => {
          if (method === 'terminal/create') return 'term-1'
          if (method === 'terminal/write') return undefined
          if (method === 'terminal/waitForExit') {
            return { exitCode: 0, output: 'hello\n', killed: false }
          }
          return undefined
        }) as AcpTransport['request'],
      })

      bridge.setTransport(transport)
      bridge.setCapabilities(fullCapabilities())

      const result = await bridge.execute('echo hello', '/tmp')

      expect(result.exitCode).toBe(0)
      expect(result.output).toBe('hello\n')
      expect(result.killed).toBe(false)
      expect(transport.request).toHaveBeenCalledWith(
        'terminal/create',
        expect.objectContaining({ cwd: '/tmp' })
      )
      expect(transport.request).toHaveBeenCalledWith(
        'terminal/write',
        expect.objectContaining({
          terminalId: 'term-1',
          data: 'echo hello\n',
        })
      )
    })

    it('should handle non-zero exit code', async () => {
      const transport = mockTransport({
        request: vi.fn(async (method: string) => {
          if (method === 'terminal/create') return 'term-2'
          if (method === 'terminal/waitForExit') {
            return { exitCode: 1, output: 'error\n', killed: false }
          }
          return undefined
        }) as AcpTransport['request'],
      })

      bridge.setTransport(transport)
      bridge.setCapabilities(fullCapabilities())

      const result = await bridge.execute('false', '/tmp')
      expect(result.exitCode).toBe(1)
    })

    it('should try to kill terminal on error', async () => {
      const killFn = vi.fn(async () => undefined)
      const transport = mockTransport({
        request: vi.fn(async (method: string) => {
          if (method === 'terminal/create') return 'term-err'
          if (method === 'terminal/write') throw new Error('write failed')
          if (method === 'terminal/kill') return killFn()
          return undefined
        }) as AcpTransport['request'],
      })

      bridge.setTransport(transport)
      bridge.setCapabilities(fullCapabilities())

      await expect(bridge.execute('cmd', '/tmp')).rejects.toThrow('write failed')
      expect(transport.request).toHaveBeenCalledWith('terminal/kill', { terminalId: 'term-err' })
    })
  })

  describe('kill', () => {
    it('should do nothing without transport', async () => {
      await expect(bridge.kill('term-1')).resolves.toBeUndefined()
    })
  })

  describe('killAll', () => {
    it('should kill all active terminals', async () => {
      // No active terminals - should complete without error
      await expect(bridge.killAll()).resolves.toBeUndefined()
    })
  })

  describe('getActiveCount', () => {
    it('should start at zero', () => {
      expect(bridge.getActiveCount()).toBe(0)
    })
  })

  describe('factory', () => {
    it('should create bridge with factory', () => {
      const b = createAcpTerminalBridge()
      expect(b).toBeInstanceOf(AcpTerminalBridge)
      expect(b.isAvailable()).toBe(false)
    })
  })
})
