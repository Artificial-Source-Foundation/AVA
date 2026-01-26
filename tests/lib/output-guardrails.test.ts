/**
 * Output Guardrails Tests
 */

import { describe, it, expect } from 'vitest'
import {
  guardrailOutput,
  shouldSkipGuardrails,
  getToolLimit,
  truncateWithBoundaries,
} from '../../src/lib/output-guardrails.js'

describe('shouldSkipGuardrails', () => {
  it('should skip Delta9 coordination tools', () => {
    expect(shouldSkipGuardrails('mission_status')).toBe(true)
    expect(shouldSkipGuardrails('council_convene')).toBe(true)
    expect(shouldSkipGuardrails('delegate_task')).toBe(true)
    expect(shouldSkipGuardrails('dispatch_task')).toBe(true)
  })

  it('should skip background task tools', () => {
    expect(shouldSkipGuardrails('background_output')).toBe(true)
    expect(shouldSkipGuardrails('background_list')).toBe(true)
  })

  it('should not skip regular tools', () => {
    expect(shouldSkipGuardrails('Read')).toBe(false)
    expect(shouldSkipGuardrails('bash')).toBe(false)
    expect(shouldSkipGuardrails('Glob')).toBe(false)
  })

  it('should be case-insensitive', () => {
    expect(shouldSkipGuardrails('MISSION_STATUS')).toBe(true)
    expect(shouldSkipGuardrails('Mission_Status')).toBe(true)
  })
})

describe('getToolLimit', () => {
  it('should return higher limit for code tools', () => {
    expect(getToolLimit('Read')).toBe(64000)
    expect(getToolLimit('read')).toBe(64000)
    expect(getToolLimit('Grep')).toBe(64000)
    expect(getToolLimit('Glob')).toBe(64000)
  })

  it('should return default limit for other tools', () => {
    expect(getToolLimit('bash')).toBe(32000)
    expect(getToolLimit('unknown_tool')).toBe(32000)
  })
})

describe('guardrailOutput', () => {
  it('should not truncate small outputs', () => {
    const result = guardrailOutput('bash', 'Hello, world!')

    expect(result.wasTruncated).toBe(false)
    expect(result.output).toBe('Hello, world!')
    expect(result.originalLength).toBe(13)
  })

  it('should truncate large outputs', () => {
    const largeOutput = 'x'.repeat(50000)
    const result = guardrailOutput('bash', largeOutput)

    expect(result.wasTruncated).toBe(true)
    expect(result.truncatedLength).toBeLessThan(result.originalLength)
    expect(result.output).toContain('[OUTPUT TRUNCATED')
  })

  it('should skip guardrails for excluded tools', () => {
    const largeOutput = 'x'.repeat(50000)
    const result = guardrailOutput('mission_status', largeOutput)

    expect(result.wasTruncated).toBe(false)
    expect(result.output).toBe(largeOutput)
  })

  it('should use higher limit for code tools', () => {
    // Just under code limit
    const mediumOutput = 'x'.repeat(40000)
    const result = guardrailOutput('Read', mediumOutput)

    expect(result.wasTruncated).toBe(false)
  })
})

describe('truncateWithBoundaries', () => {
  it('should not truncate if under limit', () => {
    const result = truncateWithBoundaries('Hello, world!', 100)
    expect(result).toBe('Hello, world!')
  })

  it('should truncate at paragraph boundary when possible', () => {
    const text = 'First paragraph.\n\nSecond paragraph.\n\nThird paragraph.'
    const result = truncateWithBoundaries(text, 40)

    expect(result).toContain('First paragraph.')
    expect(result).toContain('[OUTPUT TRUNCATED')
  })

  it('should preserve complete code blocks when possible', () => {
    const text = '```javascript\nconst x = 1;\n```\n\nMore content here that goes on and on and on'
    const result = truncateWithBoundaries(text, 50)

    // Should try to include the complete code block
    expect(result).toContain('```javascript')
    expect(result).toContain('```')
  })

  it('should include truncation message', () => {
    const largeText = 'x'.repeat(10000)
    const result = truncateWithBoundaries(largeText, 1000)

    expect(result).toContain('[OUTPUT TRUNCATED')
    expect(result).toContain('characters omitted')
  })
})
