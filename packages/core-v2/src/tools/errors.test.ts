import { describe, expect, it } from 'vitest'
import { ToolError, ToolErrorType } from './errors.js'

describe('ToolError', () => {
  it('creates error with message, type, and tool name', () => {
    const err = new ToolError('file not found', ToolErrorType.FILE_NOT_FOUND, 'read_file')
    expect(err.message).toBe('file not found')
    expect(err.type).toBe(ToolErrorType.FILE_NOT_FOUND)
    expect(err.toolName).toBe('read_file')
    expect(err.name).toBe('ToolError')
  })

  it('creates error without tool name', () => {
    const err = new ToolError('test', ToolErrorType.UNKNOWN)
    expect(err.toolName).toBeUndefined()
  })

  it('is instanceof Error', () => {
    const err = new ToolError('test', ToolErrorType.UNKNOWN)
    expect(err).toBeInstanceOf(Error)
  })

  it('is instanceof ToolError', () => {
    const err = new ToolError('test', ToolErrorType.UNKNOWN)
    expect(err).toBeInstanceOf(ToolError)
  })
})

describe('ToolError.from', () => {
  it('returns ToolError unchanged', () => {
    const original = new ToolError('test', ToolErrorType.BINARY_FILE, 'read_file')
    const result = ToolError.from(original, 'other_tool')
    expect(result).toBe(original)
    expect(result.toolName).toBe('read_file') // preserves original tool name
  })

  it('wraps regular Error', () => {
    const result = ToolError.from(new Error('something broke'), 'bash')
    expect(result).toBeInstanceOf(ToolError)
    expect(result.message).toBe('something broke')
    expect(result.toolName).toBe('bash')
  })

  it('wraps string', () => {
    const result = ToolError.from('oops', 'bash')
    expect(result.message).toBe('oops')
  })

  it('wraps number', () => {
    const result = ToolError.from(42, 'bash')
    expect(result.message).toBe('42')
  })

  // ─── Error type inference ─────────────────────────────────────────────

  it('infers FILE_NOT_FOUND from ENOENT', () => {
    const result = ToolError.from(new Error('ENOENT: no such file'), 'read_file')
    expect(result.type).toBe(ToolErrorType.FILE_NOT_FOUND)
  })

  it('infers FILE_NOT_FOUND from "not found"', () => {
    const result = ToolError.from(new Error('file not found'), 'read_file')
    expect(result.type).toBe(ToolErrorType.FILE_NOT_FOUND)
  })

  it('infers FILE_ALREADY_EXISTS from EEXIST', () => {
    const result = ToolError.from(new Error('EEXIST: file already exists'), 'write')
    expect(result.type).toBe(ToolErrorType.FILE_ALREADY_EXISTS)
  })

  it('infers PERMISSION_DENIED from EPERM', () => {
    const result = ToolError.from(new Error('EPERM: operation not permitted'), 'write')
    expect(result.type).toBe(ToolErrorType.PERMISSION_DENIED)
  })

  it('infers PERMISSION_DENIED from EACCES', () => {
    const result = ToolError.from(new Error('EACCES: permission denied'), 'write')
    expect(result.type).toBe(ToolErrorType.PERMISSION_DENIED)
  })

  it('infers BINARY_FILE from "binary"', () => {
    const result = ToolError.from(new Error('binary file detected'), 'read_file')
    expect(result.type).toBe(ToolErrorType.BINARY_FILE)
  })

  it('infers PATH_IS_DIRECTORY from "is a directory"', () => {
    const result = ToolError.from(new Error('is a directory'), 'read_file')
    expect(result.type).toBe(ToolErrorType.PATH_IS_DIRECTORY)
  })

  it('infers EXECUTION_TIMEOUT from "timeout"', () => {
    const result = ToolError.from(new Error('timeout reached'), 'bash')
    expect(result.type).toBe(ToolErrorType.EXECUTION_TIMEOUT)
  })

  it('infers EXECUTION_ABORTED from "abort"', () => {
    const result = ToolError.from(new Error('operation aborted'), 'bash')
    expect(result.type).toBe(ToolErrorType.EXECUTION_ABORTED)
  })

  it('defaults to UNKNOWN for unrecognized messages', () => {
    const result = ToolError.from(new Error('something weird'), 'bash')
    expect(result.type).toBe(ToolErrorType.UNKNOWN)
  })
})

describe('ToolErrorType', () => {
  it('has all expected types', () => {
    expect(ToolErrorType.INVALID_PARAMS).toBe('INVALID_PARAMS')
    expect(ToolErrorType.FILE_NOT_FOUND).toBe('FILE_NOT_FOUND')
    expect(ToolErrorType.FILE_ALREADY_EXISTS).toBe('FILE_ALREADY_EXISTS')
    expect(ToolErrorType.PERMISSION_DENIED).toBe('PERMISSION_DENIED')
    expect(ToolErrorType.BINARY_FILE).toBe('BINARY_FILE')
    expect(ToolErrorType.PATH_IS_DIRECTORY).toBe('PATH_IS_DIRECTORY')
    expect(ToolErrorType.INVALID_PATTERN).toBe('INVALID_PATTERN')
    expect(ToolErrorType.CONTENT_TOO_LARGE).toBe('CONTENT_TOO_LARGE')
    expect(ToolErrorType.EXECUTION_TIMEOUT).toBe('EXECUTION_TIMEOUT')
    expect(ToolErrorType.EXECUTION_ABORTED).toBe('EXECUTION_ABORTED')
    expect(ToolErrorType.BINARY_OUTPUT).toBe('BINARY_OUTPUT')
    expect(ToolErrorType.INACTIVITY_TIMEOUT).toBe('INACTIVITY_TIMEOUT')
    expect(ToolErrorType.NOT_SUPPORTED).toBe('NOT_SUPPORTED')
    expect(ToolErrorType.UNKNOWN).toBe('UNKNOWN')
  })
})
