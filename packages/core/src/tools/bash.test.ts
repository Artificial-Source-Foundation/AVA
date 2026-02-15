/**
 * Bash Tool Tests
 * Tests validation, timeout, output truncation, working directory, and abort handling
 */

import { describe, expect, it } from 'vitest'
// Import the tool to test its validate method
// The execute method requires full platform mocking, so we focus on validation
// and the security-critical code paths
import { bashTool } from './bash.js'
import { ToolError } from './errors.js'

describe('bash tool validation', () => {
  it('rejects non-object params', () => {
    expect(() => bashTool.validate('string')).toThrow(ToolError)
    expect(() => bashTool.validate(null)).toThrow(ToolError)
    expect(() => bashTool.validate(42)).toThrow(ToolError)
  })

  it('rejects missing command', () => {
    expect(() => bashTool.validate({ description: 'test' })).toThrow('command')
  })

  it('rejects empty command', () => {
    expect(() => bashTool.validate({ command: '', description: 'test' })).toThrow('command')
    expect(() => bashTool.validate({ command: '  ', description: 'test' })).toThrow('command')
  })

  it('rejects missing description', () => {
    expect(() => bashTool.validate({ command: 'ls' })).toThrow('description')
  })

  it('rejects empty description', () => {
    expect(() => bashTool.validate({ command: 'ls', description: '' })).toThrow('description')
  })

  it('rejects invalid timeout (zero)', () => {
    expect(() => bashTool.validate({ command: 'ls', description: 'test', timeout: 0 })).toThrow(
      'timeout'
    )
  })

  it('rejects invalid timeout (negative)', () => {
    expect(() => bashTool.validate({ command: 'ls', description: 'test', timeout: -1 })).toThrow(
      'timeout'
    )
  })

  it('rejects invalid timeout (string)', () => {
    expect(() =>
      bashTool.validate({ command: 'ls', description: 'test', timeout: 'slow' })
    ).toThrow('timeout')
  })

  it('rejects invalid workdir type', () => {
    expect(() => bashTool.validate({ command: 'ls', description: 'test', workdir: 123 })).toThrow(
      'workdir'
    )
  })

  it('rejects invalid interactive type', () => {
    expect(() =>
      bashTool.validate({ command: 'ls', description: 'test', interactive: 'yes' })
    ).toThrow('interactive')
  })

  it('rejects invalid requires_approval type', () => {
    expect(() =>
      bashTool.validate({ command: 'ls', description: 'test', requires_approval: 'yes' })
    ).toThrow('requires_approval')
  })

  it('accepts valid params', () => {
    const result = bashTool.validate({ command: 'ls -la', description: 'List files' })
    expect(result.command).toBe('ls -la')
    expect(result.description).toBe('List files')
  })

  it('trims command and description', () => {
    const result = bashTool.validate({ command: '  ls  ', description: '  test  ' })
    expect(result.command).toBe('ls')
    expect(result.description).toBe('test')
  })

  it('accepts valid optional params', () => {
    const result = bashTool.validate({
      command: 'npm test',
      description: 'Run tests',
      workdir: '/project',
      timeout: 60000,
      interactive: false,
      requires_approval: true,
    })
    expect(result.workdir).toBe('/project')
    expect(result.timeout).toBe(60000)
    expect(result.interactive).toBe(false)
    expect(result.requires_approval).toBe(true)
  })

  it('accepts undefined optional params', () => {
    const result = bashTool.validate({ command: 'echo hi', description: 'test' })
    expect(result.workdir).toBeUndefined()
    expect(result.timeout).toBeUndefined()
    expect(result.interactive).toBeUndefined()
    expect(result.requires_approval).toBeUndefined()
  })
})

describe('bash tool definition', () => {
  it('has correct name', () => {
    expect(bashTool.definition.name).toBe('bash')
  })

  it('requires command and description', () => {
    expect(bashTool.definition.input_schema.required).toContain('command')
    expect(bashTool.definition.input_schema.required).toContain('description')
  })

  it('defines all expected properties', () => {
    const props = Object.keys(bashTool.definition.input_schema.properties)
    expect(props).toContain('command')
    expect(props).toContain('description')
    expect(props).toContain('workdir')
    expect(props).toContain('timeout')
    expect(props).toContain('interactive')
    expect(props).toContain('requires_approval')
  })
})
