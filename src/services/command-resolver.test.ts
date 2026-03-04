import { beforeEach, describe, expect, it, vi } from 'vitest'
import { parseSlashCommand } from './command-resolver'

// Mock getCommands from core-v2
const mockCommands = new Map<
  string,
  { name: string; description: string; execute: ReturnType<typeof vi.fn> }
>()

vi.mock('@ava/core-v2/extensions', () => ({
  getCommands: () => mockCommands,
}))

// Import after mock setup
const { resolveCommand, getAvailableCommands } = await import('./command-resolver')

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
    // /home starts with a letter, but /home/user contains a slash after
    // Actually this WILL parse as name=home, args=user/file
    // The regex requires ^\/([a-zA-Z][\w-]*)(?:\s+(.*))?$
    // /home/user/file — the second / is not whitespace, so the \w- group stops at /
    // Actually: [a-zA-Z][\w-]* matches "home" then we need whitespace or end
    // /home/user/file — after "home" we have "/" which is neither \s nor end → no match
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
  beforeEach(() => {
    mockCommands.clear()
  })

  it('resolves a registered command', () => {
    mockCommands.set('help', {
      name: 'help',
      description: 'Show help',
      execute: vi.fn(),
    })

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
    mockCommands.set('deploy', {
      name: 'deploy',
      description: 'Deploy the app',
      execute: vi.fn(),
    })

    const result = resolveCommand({ name: 'deploy', args: 'staging' })
    expect(result).not.toBeNull()
    expect(result!.isBuiltIn).toBe(false)
    expect(result!.args).toBe('staging')
  })
})

describe('getAvailableCommands', () => {
  beforeEach(() => {
    mockCommands.clear()
  })

  it('returns empty array when no commands registered', () => {
    expect(getAvailableCommands()).toEqual([])
  })

  it('returns sorted command list', () => {
    mockCommands.set('zzz', { name: 'zzz', description: 'Last', execute: vi.fn() })
    mockCommands.set('aaa', { name: 'aaa', description: 'First', execute: vi.fn() })
    mockCommands.set('mmm', { name: 'mmm', description: 'Middle', execute: vi.fn() })

    const cmds = getAvailableCommands()
    expect(cmds).toHaveLength(3)
    expect(cmds[0]!.name).toBe('aaa')
    expect(cmds[1]!.name).toBe('mmm')
    expect(cmds[2]!.name).toBe('zzz')
  })

  it('includes name, description, and isBuiltIn', () => {
    mockCommands.set('help', { name: 'help', description: 'Show help info', execute: vi.fn() })
    mockCommands.set('deploy', { name: 'deploy', description: 'Deploy app', execute: vi.fn() })

    const cmds = getAvailableCommands()
    const help = cmds.find((c) => c.name === 'help')!
    const deploy = cmds.find((c) => c.name === 'deploy')!
    expect(help).toEqual({ name: 'help', description: 'Show help info', isBuiltIn: true })
    expect(deploy).toEqual({ name: 'deploy', description: 'Deploy app', isBuiltIn: false })
  })
})
