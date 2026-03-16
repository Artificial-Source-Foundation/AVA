import { describe, expect, it, vi } from 'vitest'
import {
  getAvailableCommands,
  parseSlashCommand,
  registerCommand,
  resolveCommand,
} from './command-resolver'


describe('parseSlashCommand', () => {
  it('parses /help', () => {
    expect(parseSlashCommand('/help')).toEqual({ name: 'help', args: '' })
  })

  it('parses /mode plan', () => {
    expect(parseSlashCommand('/mode plan')).toEqual({ name: 'mode', args: 'plan' })
  })

  it('parses /model with multi-word args', () => {
    expect(parseSlashCommand('/model claude-sonnet-4')).toEqual({
      name: 'model',
      args: 'claude-sonnet-4',
    })
  })

  it('parses /my-custom-cmd with multiple args', () => {
    expect(parseSlashCommand('/my-custom-cmd foo bar baz')).toEqual({
      name: 'my-custom-cmd',
      args: 'foo bar baz',
    })
  })

  it('returns null for regular messages', () => {
    expect(parseSlashCommand('hello world')).toBeNull()
  })

  it('returns null for file paths like /home/user/file', () => {
    expect(parseSlashCommand('/home/user/file')).toBeNull()
  })

  it('returns null for empty input', () => {
    expect(parseSlashCommand('')).toBeNull()
  })

  it('returns null for just /', () => {
    expect(parseSlashCommand('/')).toBeNull()
  })

  it('returns null for /123 (must start with letter)', () => {
    expect(parseSlashCommand('/123')).toBeNull()
  })

  it('trims args whitespace', () => {
    expect(parseSlashCommand('/cmd   spaced   args  ')).toEqual({
      name: 'cmd',
      args: 'spaced   args',
    })
  })
})

describe('resolveCommand', () => {
  it('resolves a registered command', () => {
    registerCommand('help', { description: 'Show help', execute: vi.fn() })

    const result = resolveCommand({ name: 'help', args: '' })
    expect(result).not.toBeNull()
    expect(result!.name).toBe('help')
    expect(result!.isBuiltIn).toBe(true)
  })

  it('returns null for unregistered commands', () => {
    const result = resolveCommand({ name: 'nonexistent', args: '' })
    expect(result).toBeNull()
  })

  it('marks custom commands as not built-in', () => {
    registerCommand('deploy', { description: 'Deploy the app', execute: vi.fn() })

    const result = resolveCommand({ name: 'deploy', args: 'staging' })
    expect(result).not.toBeNull()
    expect(result!.isBuiltIn).toBe(false)
    expect(result!.args).toBe('staging')
  })
})

describe('getAvailableCommands', () => {
  it('returns sorted command list', () => {
    registerCommand('zzz', { description: 'Last', execute: vi.fn() })
    registerCommand('aaa', { description: 'First', execute: vi.fn() })
    registerCommand('mmm', { description: 'Middle', execute: vi.fn() })

    const cmds = getAvailableCommands()
    // At minimum, aaa should come before mmm, and mmm before zzz
    const names = cmds.map((c) => c.name)
    expect(names.indexOf('aaa')).toBeLessThan(names.indexOf('mmm'))
    expect(names.indexOf('mmm')).toBeLessThan(names.indexOf('zzz'))
  })

  it('includes name, description, and isBuiltIn', () => {
    registerCommand('help', { description: 'Show help info', execute: vi.fn() })
    registerCommand('deploy', { description: 'Deploy app', execute: vi.fn() })

    const cmds = getAvailableCommands()
    const help = cmds.find((c) => c.name === 'help')!
    const deploy = cmds.find((c) => c.name === 'deploy')!
    expect(help).toEqual({ name: 'help', description: 'Show help info', isBuiltIn: true })
    expect(deploy).toEqual({ name: 'deploy', description: 'Deploy app', isBuiltIn: false })
  })
})
