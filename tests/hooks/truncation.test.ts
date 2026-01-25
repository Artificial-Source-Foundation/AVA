/**
 * Tests for Delta9 Output Truncation Hooks
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  truncateOutput,
  createTruncationHooks,
  getTruncationStats,
  clearTruncationStats,
  type TruncationConfig,
} from '../../src/hooks/truncation.js'
import type { MissionState } from '../../src/mission/state.js'

// Create mock mission state
function createMockState(): MissionState {
  return {
    getMission: () => null,
  } as unknown as MissionState
}

describe('TruncationHooks', () => {
  const log = vi.fn()

  beforeEach(() => {
    vi.clearAllMocks()
    clearTruncationStats()
    log.mockClear()
  })

  describe('truncateOutput', () => {
    it('should not truncate output under limit', () => {
      const result = truncateOutput({
        tool: 'Read',
        output: 'Hello, world!',
      })

      expect(result.wasTruncated).toBe(false)
      expect(result.output).toBe('Hello, world!')
      expect(result.originalLength).toBe(13)
      expect(result.truncatedLength).toBe(13)
    })

    it('should truncate output over default limit', () => {
      const longOutput = 'x'.repeat(50000)
      const config: TruncationConfig = {
        defaultLimit: 1000,
        toolLimits: {},
        headTailBalance: 0.3,
        smartTruncation: false,
        warningThreshold: 500,
      }

      const result = truncateOutput(
        {
          tool: 'UnknownTool',
          output: longOutput,
        },
        config
      )

      expect(result.wasTruncated).toBe(true)
      expect(result.originalLength).toBe(50000)
      expect(result.truncatedLength).toBeLessThan(50000)
      expect(result.output).toContain('truncated')
    })

    it('should use tool-specific limits', () => {
      const output = 'x'.repeat(15000)
      const config: TruncationConfig = {
        defaultLimit: 32000,
        toolLimits: {
          Grep: 10000,
        },
        headTailBalance: 0.3,
        smartTruncation: false,
        warningThreshold: 5000,
      }

      const result = truncateOutput(
        {
          tool: 'Grep',
          output,
        },
        config
      )

      expect(result.wasTruncated).toBe(true)
      expect(result.truncatedLength).toBeLessThan(15000)
    })

    it('should preserve head and tail balance', () => {
      const lines = Array.from({ length: 100 }, (_, i) => `Line ${i + 1}`).join('\n')
      const config: TruncationConfig = {
        defaultLimit: 500,
        toolLimits: {},
        headTailBalance: 0.3,
        smartTruncation: true,
        warningThreshold: 200,
      }

      const result = truncateOutput(
        {
          tool: 'Test',
          output: lines,
        },
        config
      )

      expect(result.wasTruncated).toBe(true)
      expect(result.output).toContain('Line 1')
      expect(result.output).toContain('Line 100')
    })

    it('should handle JSON truncation', () => {
      const jsonArray = JSON.stringify(Array.from({ length: 100 }, (_, i) => ({ id: i, name: `Item ${i}` })))
      const config: TruncationConfig = {
        defaultLimit: 500,
        toolLimits: {},
        headTailBalance: 0.3,
        smartTruncation: true,
        warningThreshold: 200,
      }

      const result = truncateOutput(
        {
          tool: 'Test',
          output: jsonArray,
        },
        config
      )

      expect(result.wasTruncated).toBe(true)
      expect(result.output).toContain('truncated')
    })

    it('should update truncation stats', () => {
      const longOutput = 'x'.repeat(2000)
      const config: TruncationConfig = {
        defaultLimit: 1000,
        toolLimits: {},
        headTailBalance: 0.3,
        smartTruncation: false,
        warningThreshold: 500,
      }

      truncateOutput({ tool: 'TestTool', output: longOutput }, config)
      truncateOutput({ tool: 'TestTool', output: longOutput }, config)

      const stats = getTruncationStats()
      expect(stats.totalTruncations).toBe(2)
      expect(stats.totalCharsSaved).toBeGreaterThan(0)
      expect(stats.byTool['TestTool'].count).toBe(2)
    })
  })

  describe('createTruncationHooks', () => {
    it('should create hook with tool.execute.after', () => {
      const hooks = createTruncationHooks({ state: createMockState(), log })

      expect(hooks['tool.execute.after']).toBeDefined()
      expect(typeof hooks['tool.execute.after']).toBe('function')
    })

    it('should truncate tool output in hook', async () => {
      const hooks = createTruncationHooks({
        state: createMockState(),
        log,
        config: {
          defaultLimit: 100,
          toolLimits: {},
          headTailBalance: 0.3,
          smartTruncation: false,
          warningThreshold: 50,
        },
      })

      const longOutput = 'x'.repeat(500)
      const toolInput = {
        tool: 'TestTool',
        sessionID: 'session-1',
        callID: 'call-1',
      }
      const toolOutput = {
        output: longOutput,
        metadata: {},
        title: 'Test Tool',
      }

      await hooks['tool.execute.after'](toolInput, toolOutput)

      expect(toolOutput.output.length).toBeLessThan(500)
      expect(toolOutput.metadata).toHaveProperty('truncation')
      expect((toolOutput.metadata as { truncation: { wasTruncated: boolean } }).truncation.wasTruncated).toBe(true)
    })

    it('should log warning for large truncations', async () => {
      const hooks = createTruncationHooks({
        state: createMockState(),
        log,
        config: {
          defaultLimit: 100,
          toolLimits: {},
          headTailBalance: 0.3,
          smartTruncation: false,
          warningThreshold: 200,
        },
      })

      const longOutput = 'x'.repeat(500)
      const toolInput = {
        tool: 'TestTool',
        sessionID: 'session-1',
        callID: 'call-1',
      }
      const toolOutput = {
        output: longOutput,
        metadata: {},
        title: 'Test Tool',
      }

      await hooks['tool.execute.after'](toolInput, toolOutput)

      expect(log).toHaveBeenCalledWith(
        'warn',
        expect.stringContaining('Output truncated'),
        expect.objectContaining({
          originalLength: 500,
        })
      )
    })

    it('should log debug for small truncations', async () => {
      const hooks = createTruncationHooks({
        state: createMockState(),
        log,
        config: {
          defaultLimit: 100,
          toolLimits: {},
          headTailBalance: 0.3,
          smartTruncation: false,
          warningThreshold: 1000,
        },
      })

      const output = 'x'.repeat(200)
      const toolInput = {
        tool: 'TestTool',
        sessionID: 'session-1',
        callID: 'call-1',
      }
      const toolOutput = {
        output,
        metadata: {},
        title: 'Test Tool',
      }

      await hooks['tool.execute.after'](toolInput, toolOutput)

      expect(log).toHaveBeenCalledWith(
        'debug',
        expect.stringContaining('Output truncated'),
        expect.any(Object)
      )
    })

    it('should not modify output under limit', async () => {
      const hooks = createTruncationHooks({
        state: createMockState(),
        log,
        config: {
          defaultLimit: 1000,
          toolLimits: {},
          headTailBalance: 0.3,
          smartTruncation: false,
          warningThreshold: 500,
        },
      })

      const shortOutput = 'Hello, world!'
      const toolInput = {
        tool: 'TestTool',
        sessionID: 'session-1',
        callID: 'call-1',
      }
      const toolOutput = {
        output: shortOutput,
        metadata: {},
        title: 'Test Tool',
      }

      await hooks['tool.execute.after'](toolInput, toolOutput)

      expect(toolOutput.output).toBe(shortOutput)
      expect(toolOutput.metadata).not.toHaveProperty('truncation')
    })
  })

  describe('getTruncationStats', () => {
    it('should return empty stats initially', () => {
      const stats = getTruncationStats()
      expect(stats.totalTruncations).toBe(0)
      expect(stats.totalCharsSaved).toBe(0)
      expect(Object.keys(stats.byTool)).toHaveLength(0)
    })
  })

  describe('clearTruncationStats', () => {
    it('should clear all stats', () => {
      const config: TruncationConfig = {
        defaultLimit: 100,
        toolLimits: {},
        headTailBalance: 0.3,
        smartTruncation: false,
        warningThreshold: 50,
      }

      truncateOutput({ tool: 'Test', output: 'x'.repeat(500) }, config)
      expect(getTruncationStats().totalTruncations).toBe(1)

      clearTruncationStats()
      expect(getTruncationStats().totalTruncations).toBe(0)
    })
  })

  describe('Smart Truncation', () => {
    const smartConfig: TruncationConfig = {
      defaultLimit: 200,
      toolLimits: {},
      headTailBalance: 0.3,
      smartTruncation: true,
      warningThreshold: 100,
    }

    it('should handle line-based content', () => {
      const lines = Array.from({ length: 50 }, (_, i) => `Line ${i + 1}: Some content here`).join('\n')

      const result = truncateOutput({ tool: 'Test', output: lines }, smartConfig)

      expect(result.wasTruncated).toBe(true)
      expect(result.output).toContain('lines truncated')
    })

    it('should handle JSON objects', () => {
      const obj = { name: 'test', value: 'x'.repeat(500), nested: { a: 1, b: 2 } }
      const json = JSON.stringify(obj, null, 2)

      const result = truncateOutput({ tool: 'Test', output: json }, smartConfig)

      expect(result.wasTruncated).toBe(true)
    })

    it('should handle code-like content', () => {
      const code = `
function hello() {
  const x = 1;
  const y = 2;
  return x + y;
}

function world() {
  const a = 3;
  const b = 4;
  return a + b;
}

// More content
${'const z = 0;\n'.repeat(50)}
`

      const result = truncateOutput({ tool: 'Test', output: code }, smartConfig)

      expect(result.wasTruncated).toBe(true)
    })
  })
})
