/**
 * Edit Error Recovery Tests
 *
 * Uses representative sampling - one error message per type.
 */

import { describe, it, expect } from 'vitest'
import {
  detectEditError,
  generateRecoveryMessage,
  isEditTool,
  getEditErrorLabel,
  type EditErrorType,
} from '../../src/hooks/edit-error-recovery.js'

describe('detectEditError', () => {
  it('should detect each error type from representative messages', () => {
    // One representative per error type
    expect(detectEditError('oldString and newString must be different')?.errorType).toBe('no_change')
    expect(detectEditError('oldString not found in file')?.errorType).toBe('not_found')
    expect(detectEditError('oldString found multiple times')?.errorType).toBe('ambiguous')
    expect(detectEditError('file not found')?.errorType).toBe('file_missing')
    expect(detectEditError('ENOENT: no such file')?.errorType).toBe('file_missing')
    expect(detectEditError('permission denied')?.errorType).toBe('permission')
    expect(detectEditError('Syntax error in replacement')?.errorType).toBe('syntax')
    expect(detectEditError('Failed to edit file: unknown')?.errorType).toBe('unknown')
  })

  it('should detect error from Error object and prefer it over output', () => {
    const error = new Error('oldString not found')
    expect(detectEditError('', error)?.errorType).toBe('not_found')

    // Error object takes precedence
    const permError = new Error('permission denied')
    expect(detectEditError('oldString not found', permError)?.errorType).toBe('permission')
  })

  it('should return null for non-error outputs', () => {
    expect(detectEditError('File edited successfully')).toBeNull()
    expect(detectEditError('')).toBeNull()
    expect(detectEditError('The user reported an error in their workflow')).toBeNull()
  })
})

describe('generateRecoveryMessage', () => {
  it('should generate appropriate messages for each error type', () => {
    const notFound = generateRecoveryMessage({ errorType: 'not_found', originalMessage: 'test' })
    expect(notFound).toContain('TEXT NOT FOUND')
    expect(notFound).toContain('READ the file')

    const ambiguous = generateRecoveryMessage({ errorType: 'ambiguous', originalMessage: 'test' })
    expect(ambiguous).toContain('MULTIPLE MATCHES')
    expect(ambiguous).toContain('MORE context')

    const noChange = generateRecoveryMessage({ errorType: 'no_change', originalMessage: 'test' })
    expect(noChange).toContain('NO CHANGE')

    const fileMissing = generateRecoveryMessage({ errorType: 'file_missing', originalMessage: 'test' })
    expect(fileMissing).toContain('FILE NOT FOUND')
    expect(fileMissing).toContain('Glob')
  })

  it('should include original message and context', () => {
    const message = generateRecoveryMessage({
      errorType: 'not_found',
      originalMessage: 'specific error',
      context: 'Additional context',
    })
    expect(message).toContain('specific error')
    expect(message).toContain('Additional context')
  })

  it('should always warn against blind retries', () => {
    const message = generateRecoveryMessage({ errorType: 'unknown', originalMessage: 'test' })
    expect(message).toContain('DO NOT retry blindly')
  })
})

describe('isEditTool', () => {
  it('should identify edit tools correctly', () => {
    // Edit tools
    const editTools = ['Edit', 'edit', 'Write', 'MultiEdit', 'file_edit']
    for (const tool of editTools) {
      expect(isEditTool(tool)).toBe(true)
    }

    // Non-edit tools
    const nonEditTools = ['Read', 'Glob', 'Grep', 'Bash']
    for (const tool of nonEditTools) {
      expect(isEditTool(tool)).toBe(false)
    }
  })
})

describe('getEditErrorLabel', () => {
  it('should return human-readable labels for all error types', () => {
    const labels: Record<EditErrorType, string> = {
      no_change: 'No Change',
      not_found: 'Text Not Found',
      ambiguous: 'Multiple Matches',
      file_missing: 'File Missing',
      permission: 'Permission Denied',
      syntax: 'Syntax Error',
      unknown: 'Unknown Error',
    }

    for (const [type, label] of Object.entries(labels)) {
      expect(getEditErrorLabel(type as EditErrorType)).toBe(label)
    }
  })
})
