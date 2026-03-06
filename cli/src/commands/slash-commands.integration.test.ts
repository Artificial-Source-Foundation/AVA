/**
 * Integration test for slash commands — full pipeline.
 *
 * Loads the real slash-commands extension via ExtensionManager,
 * then verifies the command resolver works end-to-end.
 */

import { createMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { MessageBus } from '@ava/core-v2/bus'
import {
  ExtensionManager,
  getCommands,
  loadBuiltInExtension,
  resetRegistries,
} from '@ava/core-v2/extensions'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager } from '@ava/core-v2/session'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
// Import the extension module and its manifest info directly
import * as slashCommandsModule from '../../../packages/extensions/slash-commands/src/index'
import {
  getAvailableCommands,
  parseSlashCommand,
  resolveCommand,
} from '../../../src/services/command-resolver'

const manifest = {
  name: 'ava-slash-commands',
  version: '1.0.0',
  description: 'Built-in /commands',
  main: 'src/index.ts',
  builtIn: true,
  enabledByDefault: true,
  priority: 10,
}

describe('slash commands integration', () => {
  let manager: ExtensionManager

  beforeEach(async () => {
    resetRegistries()
    setPlatform(createMockPlatform())

    const bus = new MessageBus()
    const sessionManager = createSessionManager()
    manager = new ExtensionManager(bus, sessionManager)

    // Load slash-commands via direct module import
    const ext = loadBuiltInExtension(manifest, slashCommandsModule)
    manager.register(ext.manifest, ext.path)
    const modules = new Map()
    modules.set(ext.manifest.name, ext.module)
    await manager.activateAll(modules)
  })

  afterEach(async () => {
    await manager.dispose()
    resetRegistries()
  })

  // ── Registration ─────────────────────────────────────────────────────────

  it('registers all 12 built-in commands', () => {
    const commands = getCommands()
    expect(commands.size).toBe(12)
    expect(commands.has('help')).toBe(true)
    expect(commands.has('clear')).toBe(true)
    expect(commands.has('mode')).toBe(true)
    expect(commands.has('architect')).toBe(true)
    expect(commands.has('model')).toBe(true)
    expect(commands.has('compact')).toBe(true)
    expect(commands.has('undo')).toBe(true)
    expect(commands.has('redo')).toBe(true)
    expect(commands.has('settings')).toBe(true)
    expect(commands.has('status')).toBe(true)
    expect(commands.has('export')).toBe(true)
    expect(commands.has('init')).toBe(true)
  })

  // ── Parse → Resolve Pipeline ─────────────────────────────────────────────

  it('/help parses and resolves as built-in', () => {
    const parsed = parseSlashCommand('/help')
    expect(parsed).toEqual({ name: 'help', args: '' })

    const resolved = resolveCommand(parsed!)
    expect(resolved).not.toBeNull()
    expect(resolved!.isBuiltIn).toBe(true)
    expect(resolved!.name).toBe('help')
  })

  it('/mode plan parses with args', () => {
    const parsed = parseSlashCommand('/mode plan')
    expect(parsed).toEqual({ name: 'mode', args: 'plan' })

    const resolved = resolveCommand(parsed!)
    expect(resolved).not.toBeNull()
    expect(resolved!.args).toBe('plan')
  })

  it('/model claude-sonnet-4 parses correctly', () => {
    const parsed = parseSlashCommand('/model claude-sonnet-4')
    expect(parsed).toEqual({ name: 'model', args: 'claude-sonnet-4' })

    const resolved = resolveCommand(parsed!)
    expect(resolved).not.toBeNull()
    expect(resolved!.args).toBe('claude-sonnet-4')
  })

  it('/home/user/file is NOT parsed as command', () => {
    expect(parseSlashCommand('/home/user/file')).toBeNull()
  })

  it('/nonexistent parses but does not resolve', () => {
    const parsed = parseSlashCommand('/nonexistent')
    expect(parsed).toEqual({ name: 'nonexistent', args: '' })

    const resolved = resolveCommand(parsed!)
    expect(resolved).toBeNull()
  })

  it('regular messages are not parsed', () => {
    expect(parseSlashCommand('hello world')).toBeNull()
    expect(parseSlashCommand('')).toBeNull()
    expect(parseSlashCommand('/')).toBeNull()
  })

  // ── getAvailableCommands ─────────────────────────────────────────────────

  it('returns all commands sorted alphabetically', () => {
    const cmds = getAvailableCommands()
    expect(cmds.length).toBe(12)

    const names = cmds.map((c) => c.name)
    const sorted = [...names].sort()
    expect(names).toEqual(sorted)
  })

  it('marks all 12 commands as built-in', () => {
    const cmds = getAvailableCommands()
    for (const cmd of cmds) {
      expect(cmd.isBuiltIn).toBe(true)
    }
  })

  it('includes descriptions for all commands', () => {
    const cmds = getAvailableCommands()
    for (const cmd of cmds) {
      expect(cmd.description.length).toBeGreaterThan(0)
    }
  })

  // ── Command Execution ────────────────────────────────────────────────────

  const ctx = { sessionId: 'test', workingDirectory: '/tmp', signal: new AbortController().signal }

  it('/help returns feedback string', async () => {
    const cmd = getCommands().get('help')!
    const result = await cmd.execute('', ctx)
    expect(typeof result).toBe('string')
    expect(result.length).toBeGreaterThan(0)
  })

  it('/mode with no args returns usage', async () => {
    const cmd = getCommands().get('mode')!
    const result = await cmd.execute('', ctx)
    expect(result).toContain('Usage')
  })

  it('/mode plan returns confirmation', async () => {
    const cmd = getCommands().get('mode')!
    const result = await cmd.execute('plan', ctx)
    expect(result).toContain('plan')
  })

  it('/model with no args returns usage', async () => {
    const cmd = getCommands().get('model')!
    const result = await cmd.execute('', ctx)
    expect(result).toContain('Usage')
  })

  it('/model with args returns confirmation', async () => {
    const cmd = getCommands().get('model')!
    const result = await cmd.execute('gpt-4o', ctx)
    expect(result).toContain('gpt-4o')
  })

  it('/clear returns feedback', async () => {
    const cmd = getCommands().get('clear')!
    const result = await cmd.execute('', ctx)
    expect(result.toLowerCase()).toContain('clear')
  })

  // ── Full E2E: parse → resolve → execute ──────────────────────────────────

  it('full pipeline: parse → resolve → execute', async () => {
    const input = '/compact'
    const parsed = parseSlashCommand(input)
    expect(parsed).not.toBeNull()

    const resolved = resolveCommand(parsed!)
    expect(resolved).not.toBeNull()
    expect(resolved!.isBuiltIn).toBe(true)

    const result = await resolved!.command.execute(resolved!.args, ctx)
    expect(typeof result).toBe('string')
    expect(result.length).toBeGreaterThan(0)
  })
})
