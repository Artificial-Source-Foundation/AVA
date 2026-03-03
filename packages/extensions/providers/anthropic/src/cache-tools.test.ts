/**
 * Tests for tool cache marking in Anthropic provider.
 */

import { describe, expect, it } from 'vitest'
import { addToolCacheMarker } from './cache.js'

describe('addToolCacheMarker', () => {
  it('adds cache_control to the last tool', () => {
    const tools = [
      { name: 'read_file', description: 'Read', input_schema: { type: 'object', properties: {} } },
      {
        name: 'write_file',
        description: 'Write',
        input_schema: { type: 'object', properties: {} },
      },
      { name: 'bash', description: 'Shell', input_schema: { type: 'object', properties: {} } },
    ]

    const result = addToolCacheMarker(tools)

    // Last tool should have cache_control
    expect(result[2]).toHaveProperty('cache_control', { type: 'ephemeral' })

    // Other tools should not
    expect(result[0]).not.toHaveProperty('cache_control')
    expect(result[1]).not.toHaveProperty('cache_control')
  })

  it('does not mutate the original array', () => {
    const tools = [
      { name: 'read_file', description: 'Read', input_schema: { type: 'object', properties: {} } },
    ]

    const result = addToolCacheMarker(tools)

    expect(result).not.toBe(tools)
    expect(tools[0]).not.toHaveProperty('cache_control')
    expect(result[0]).toHaveProperty('cache_control', { type: 'ephemeral' })
  })

  it('returns empty array unchanged', () => {
    expect(addToolCacheMarker([])).toEqual([])
  })

  it('works with single tool', () => {
    const tools = [
      { name: 'bash', description: 'Shell', input_schema: { type: 'object', properties: {} } },
    ]
    const result = addToolCacheMarker(tools)
    expect(result).toHaveLength(1)
    expect(result[0]).toHaveProperty('cache_control', { type: 'ephemeral' })
  })
})
