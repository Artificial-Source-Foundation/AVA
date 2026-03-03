/**
 * Tests for tool cache marking in OpenRouter provider.
 */

import { describe, expect, it } from 'vitest'
import { addToolCacheMarkers } from './cache.js'

describe('addToolCacheMarkers', () => {
  it('adds cache_control to the last tool in OpenAI format', () => {
    const tools = [
      { type: 'function', function: { name: 'read_file', description: 'Read', parameters: {} } },
      { type: 'function', function: { name: 'write_file', description: 'Write', parameters: {} } },
      { type: 'function', function: { name: 'bash', description: 'Shell', parameters: {} } },
    ]

    const result = addToolCacheMarkers(tools)

    // Last tool should have cache_control
    expect(result[2]).toHaveProperty('cache_control', { type: 'ephemeral' })

    // Other tools should not
    expect(result[0]).not.toHaveProperty('cache_control')
    expect(result[1]).not.toHaveProperty('cache_control')
  })

  it('does not mutate the original array', () => {
    const tools = [
      { type: 'function', function: { name: 'bash', description: 'Shell', parameters: {} } },
    ]

    const result = addToolCacheMarkers(tools)

    expect(result).not.toBe(tools)
    expect(tools[0]).not.toHaveProperty('cache_control')
    expect(result[0]).toHaveProperty('cache_control', { type: 'ephemeral' })
  })

  it('returns empty array unchanged', () => {
    expect(addToolCacheMarkers([])).toEqual([])
  })
})
