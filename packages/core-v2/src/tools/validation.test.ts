import { describe, expect, it } from 'vitest'
import * as z from 'zod'
import { formatZodError, isZodSchema } from './validation.js'

describe('formatZodError', () => {
  it('formats single issue', () => {
    const schema = z.object({ name: z.string() })
    try {
      z.parse(schema, { name: 123 })
    } catch (err) {
      const msg = formatZodError(err as z.ZodError)
      expect(msg).toContain('Validation error')
      expect(msg).toContain('name')
    }
  })

  it('formats multiple issues', () => {
    const schema = z.object({
      name: z.string(),
      age: z.number(),
    })
    try {
      z.parse(schema, { name: 123, age: 'not a number' })
    } catch (err) {
      const msg = formatZodError(err as z.ZodError)
      expect(msg).toContain('Validation error')
    }
  })

  it('handles root-level errors', () => {
    const schema = z.string()
    try {
      z.parse(schema, 123)
    } catch (err) {
      const msg = formatZodError(err as z.ZodError)
      expect(msg).toContain('Validation error')
    }
  })
})

describe('isZodSchema', () => {
  it('returns true for Zod schemas', () => {
    expect(isZodSchema(z.string())).toBe(true)
    expect(isZodSchema(z.object({ name: z.string() }))).toBe(true)
    expect(isZodSchema(z.number())).toBe(true)
  })

  it('returns false for non-schemas', () => {
    expect(isZodSchema(null)).toBe(false)
    expect(isZodSchema(undefined)).toBe(false)
    expect(isZodSchema('string')).toBe(false)
    expect(isZodSchema(123)).toBe(false)
    expect(isZodSchema({})).toBe(false)
    expect(isZodSchema({ _def: 'not object' })).toBe(false)
  })
})
