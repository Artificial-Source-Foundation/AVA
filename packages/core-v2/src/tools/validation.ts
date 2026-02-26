/**
 * Zod validation helpers for tools.
 */

import type { ZodError } from 'zod'

export function formatZodError<T>(error: ZodError<T>): string {
  const issues = error.issues.map((issue) => {
    const path = issue.path.join('.')
    return path ? `${path}: ${issue.message}` : issue.message
  })
  return `Validation error: ${issues.join(', ')}`
}

export function isZodSchema(value: unknown): boolean {
  return (
    typeof value === 'object' &&
    value !== null &&
    '_def' in value &&
    typeof (value as Record<string, unknown>)._def === 'object'
  )
}
