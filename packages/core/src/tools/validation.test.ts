/**
 * Validation Utilities Tests
 * Tests for Zod error formatting, schema detection, and common schemas
 */

import { describe, expect, it } from 'vitest'
import { z } from 'zod'
import { commonSchemas, formatZodError, isZodSchema } from './validation.js'

// ============================================================================
// formatZodError
// ============================================================================

describe('formatZodError', () => {
  it('should format single issue with path', () => {
    const error = {
      issues: [{ path: ['name'], message: 'Required', code: 'invalid_type' }],
    }
    const result = formatZodError(error as never)
    expect(result).toBe('Validation error: name: Required')
  })

  it('should format single issue without path as "input"', () => {
    const error = {
      issues: [{ path: [], message: 'Invalid input', code: 'invalid_type' }],
    }
    const result = formatZodError(error as never)
    expect(result).toBe('Validation error: input: Invalid input')
  })

  it('should format single issue with nested path', () => {
    const error = {
      issues: [{ path: ['user', 'email'], message: 'Invalid email', code: 'invalid_string' }],
    }
    const result = formatZodError(error as never)
    expect(result).toBe('Validation error: user.email: Invalid email')
  })

  it('should format multiple issues as bulleted list', () => {
    const error = {
      issues: [
        { path: ['name'], message: 'Required', code: 'invalid_type' },
        { path: ['age'], message: 'Expected number', code: 'invalid_type' },
      ],
    }
    const result = formatZodError(error as never)
    expect(result).toBe('Validation errors:\n  - name: Required\n  - age: Expected number')
  })

  it('should format multiple issues with mixed paths', () => {
    const error = {
      issues: [
        { path: [], message: 'Invalid input', code: 'invalid_type' },
        { path: ['config', 'timeout'], message: 'Too small', code: 'too_small' },
        { path: ['name'], message: 'Too short', code: 'too_small' },
      ],
    }
    const result = formatZodError(error as never)
    expect(result).toContain('Validation errors:')
    expect(result).toContain('  - input: Invalid input')
    expect(result).toContain('  - config.timeout: Too small')
    expect(result).toContain('  - name: Too short')
  })

  it('should work with real Zod validation errors', () => {
    const schema = z.object({
      path: z.string(),
      count: z.number(),
    })
    const result = schema.safeParse({ path: 123, count: 'not a number' })
    expect(result.success).toBe(false)

    if (!result.success) {
      const formatted = formatZodError(result.error)
      expect(formatted).toContain('Validation error')
      expect(formatted).toContain('path')
      expect(formatted).toContain('count')
    }
  })

  it('should handle deeply nested paths', () => {
    const error = {
      issues: [{ path: ['a', 'b', 'c', 'd'], message: 'Deep error', code: 'custom' }],
    }
    const result = formatZodError(error as never)
    expect(result).toBe('Validation error: a.b.c.d: Deep error')
  })
})

// ============================================================================
// isZodSchema
// ============================================================================

describe('isZodSchema', () => {
  it('should return true for z.string()', () => {
    expect(isZodSchema(z.string())).toBe(true)
  })

  it('should return true for z.number()', () => {
    expect(isZodSchema(z.number())).toBe(true)
  })

  it('should return true for z.object()', () => {
    expect(isZodSchema(z.object({ name: z.string() }))).toBe(true)
  })

  it('should return true for z.array()', () => {
    expect(isZodSchema(z.array(z.string()))).toBe(true)
  })

  it('should return false for plain object', () => {
    expect(isZodSchema({ type: 'string' })).toBe(false)
  })

  it('should return false for null', () => {
    expect(isZodSchema(null)).toBe(false)
  })

  it('should return false for undefined', () => {
    expect(isZodSchema(undefined)).toBe(false)
  })

  it('should return false for string', () => {
    expect(isZodSchema('not a schema')).toBe(false)
  })

  it('should return false for number', () => {
    expect(isZodSchema(42)).toBe(false)
  })

  it('should return false for array', () => {
    expect(isZodSchema([1, 2, 3])).toBe(false)
  })

  it('should return false for object with _def but no safeParse', () => {
    expect(isZodSchema({ _def: {} })).toBe(false)
  })

  it('should return false for object with safeParse but no _def', () => {
    expect(isZodSchema({ safeParse: () => ({}) })).toBe(false)
  })

  it('should return false for object with _def and non-function safeParse', () => {
    expect(isZodSchema({ _def: {}, safeParse: 'not a function' })).toBe(false)
  })
})

// ============================================================================
// commonSchemas
// ============================================================================

describe('commonSchemas', () => {
  it('should have filePath with type string', () => {
    expect(commonSchemas.filePath.type).toBe('string')
  })

  it('should have filePath with minLength 1', () => {
    expect(commonSchemas.filePath.minLength).toBe(1)
  })

  it('should have filePath with description', () => {
    expect(commonSchemas.filePath.description).toBeTruthy()
  })

  it('should have directoryPath with type string', () => {
    expect(commonSchemas.directoryPath.type).toBe('string')
  })

  it('should have globPattern with type string', () => {
    expect(commonSchemas.globPattern.type).toBe('string')
  })

  it('should have regexPattern with type string', () => {
    expect(commonSchemas.regexPattern.type).toBe('string')
  })

  it('should have content with type string', () => {
    expect(commonSchemas.content.type).toBe('string')
  })

  it('should have lineNumber with type number', () => {
    expect(commonSchemas.lineNumber.type).toBe('number')
  })

  it('should have lineNumber with minimum 1', () => {
    expect(commonSchemas.lineNumber.minimum).toBe(1)
  })

  it('should have positiveInt with type number', () => {
    expect(commonSchemas.positiveInt.type).toBe('number')
  })

  it('should have positiveInt with minimum 1', () => {
    expect(commonSchemas.positiveInt.minimum).toBe(1)
  })
})
