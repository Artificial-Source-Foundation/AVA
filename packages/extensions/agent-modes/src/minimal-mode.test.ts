/**
 * Minimal mode — token-efficiency mode with core tools only.
 */

import type { ToolDefinition } from '@ava/core-v2/llm'
import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  enterMinimalMode,
  exitMinimalMode,
  isMinimalModeActive,
  minimalAgentMode,
  registerMinimalMode,
  resetMinimalMode,
} from './minimal-mode.js'

afterEach(() => {
  resetMinimalMode()
})

describe('minimal mode state management', () => {
  it('is not active by default', () => {
    expect(isMinimalModeActive('session-1')).toBe(false)
  })

  it('enterMinimalMode activates for session', () => {
    enterMinimalMode('session-1')
    expect(isMinimalModeActive('session-1')).toBe(true)
  })

  it('exitMinimalMode deactivates for session', () => {
    enterMinimalMode('session-1')
    exitMinimalMode('session-1')
    expect(isMinimalModeActive('session-1')).toBe(false)
  })

  it('tracks per-session state independently', () => {
    enterMinimalMode('session-1')
    expect(isMinimalModeActive('session-1')).toBe(true)
    expect(isMinimalModeActive('session-2')).toBe(false)
  })

  it('resetMinimalMode clears all sessions', () => {
    enterMinimalMode('session-1')
    enterMinimalMode('session-2')
    resetMinimalMode()
    expect(isMinimalModeActive('session-1')).toBe(false)
    expect(isMinimalModeActive('session-2')).toBe(false)
  })
})

describe('minimalAgentMode', () => {
  it('has correct name and description', () => {
    expect(minimalAgentMode.name).toBe('minimal')
    expect(minimalAgentMode.description).toBeDefined()
  })

  it('filterTools keeps only allowed tools', () => {
    const allTools: ToolDefinition[] = [
      { name: 'read_file', description: 'Read', input_schema: { type: 'object', properties: {} } },
      {
        name: 'write_file',
        description: 'Write',
        input_schema: { type: 'object', properties: {} },
      },
      { name: 'edit', description: 'Edit', input_schema: { type: 'object', properties: {} } },
      { name: 'glob', description: 'Glob', input_schema: { type: 'object', properties: {} } },
      { name: 'grep', description: 'Grep', input_schema: { type: 'object', properties: {} } },
      { name: 'bash', description: 'Bash', input_schema: { type: 'object', properties: {} } },
      {
        name: 'attempt_completion',
        description: 'Complete',
        input_schema: { type: 'object', properties: {} },
      },
      { name: 'question', description: 'Ask', input_schema: { type: 'object', properties: {} } },
      // These should be filtered out:
      { name: 'websearch', description: 'Web', input_schema: { type: 'object', properties: {} } },
      { name: 'browser', description: 'Browser', input_schema: { type: 'object', properties: {} } },
      { name: 'batch', description: 'Batch', input_schema: { type: 'object', properties: {} } },
    ]

    const filtered = minimalAgentMode.filterTools!(allTools)
    expect(filtered).toHaveLength(8)
    const names = filtered.map((t) => t.name)
    expect(names).toContain('read_file')
    expect(names).toContain('write_file')
    expect(names).toContain('edit')
    expect(names).toContain('glob')
    expect(names).toContain('grep')
    expect(names).toContain('bash')
    expect(names).toContain('attempt_completion')
    expect(names).toContain('question')
    expect(names).not.toContain('websearch')
    expect(names).not.toContain('browser')
    expect(names).not.toContain('batch')
  })

  it('filterTools returns empty when no allowed tools present', () => {
    const tools: ToolDefinition[] = [
      { name: 'websearch', description: 'Web', input_schema: { type: 'object', properties: {} } },
    ]
    const filtered = minimalAgentMode.filterTools!(tools)
    expect(filtered).toHaveLength(0)
  })

  it('systemPrompt appends minimal mode message', () => {
    const base = 'You are AVA.'
    const result = minimalAgentMode.systemPrompt!(base)
    expect(result).toContain('You are AVA.')
    expect(result).toContain('MINIMAL MODE')
    expect(result).toContain('concise')
  })
})

describe('registerMinimalMode', () => {
  it('registers with API and returns disposable', () => {
    const mockApi = {
      registerAgentMode: vi.fn().mockReturnValue({ dispose: vi.fn() }),
    }

    const disposable = registerMinimalMode(mockApi as never)
    expect(mockApi.registerAgentMode).toHaveBeenCalledWith(minimalAgentMode)
    expect(disposable).toBeDefined()
    expect(typeof disposable.dispose).toBe('function')
  })
})
