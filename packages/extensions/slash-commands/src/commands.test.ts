import { describe, expect, it, vi } from 'vitest'
import { createBuiltinCommands } from './commands.js'

describe('createBuiltinCommands', () => {
  const emit = vi.fn()
  const ctx = {
    sessionId: 'test-session',
    workingDirectory: '/tmp',
    signal: new AbortController().signal,
  }

  it('creates 9 built-in commands', () => {
    const commands = createBuiltinCommands(emit)
    expect(commands).toHaveLength(9)
  })

  it('includes expected command names', () => {
    const commands = createBuiltinCommands(emit)
    const names = commands.map((c) => c.name)
    expect(names).toContain('help')
    expect(names).toContain('clear')
    expect(names).toContain('mode')
    expect(names).toContain('model')
    expect(names).toContain('compact')
    expect(names).toContain('undo')
    expect(names).toContain('redo')
    expect(names).toContain('settings')
    expect(names).toContain('status')
  })

  it('/help emits commands:help-requested', async () => {
    const commands = createBuiltinCommands(emit)
    const help = commands.find((c) => c.name === 'help')!
    await help.execute('', ctx)
    expect(emit).toHaveBeenCalledWith('commands:help-requested', {})
  })

  it('/clear emits session:clear', async () => {
    const commands = createBuiltinCommands(emit)
    const clear = commands.find((c) => c.name === 'clear')!
    await clear.execute('', ctx)
    expect(emit).toHaveBeenCalledWith('session:clear', { sessionId: 'test-session' })
  })

  it('/mode emits mode:switch with mode name', async () => {
    const commands = createBuiltinCommands(emit)
    const mode = commands.find((c) => c.name === 'mode')!
    await mode.execute('plan', ctx)
    expect(emit).toHaveBeenCalledWith('mode:switch', { mode: 'plan' })
  })

  it('/mode returns usage when no args', async () => {
    const commands = createBuiltinCommands(emit)
    const mode = commands.find((c) => c.name === 'mode')!
    const result = await mode.execute('', ctx)
    expect(result).toContain('Usage')
  })

  it('/model emits model:switch', async () => {
    const commands = createBuiltinCommands(emit)
    const model = commands.find((c) => c.name === 'model')!
    await model.execute('claude-sonnet', ctx)
    expect(emit).toHaveBeenCalledWith('model:switch', { model: 'claude-sonnet' })
  })

  it('/compact emits context:compact', async () => {
    const commands = createBuiltinCommands(emit)
    const compact = commands.find((c) => c.name === 'compact')!
    await compact.execute('', ctx)
    expect(emit).toHaveBeenCalledWith('context:compact', { sessionId: 'test-session' })
  })
})
