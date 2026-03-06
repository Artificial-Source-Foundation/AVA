import { describe, expect, it, vi } from 'vitest'
import { createBuiltinCommands } from './commands.js'

vi.mock('./recipes.js', () => ({
  discoverRecipes: vi.fn(async () => [
    {
      name: 'add-feature',
      description: 'Standard workflow',
      version: '1.0',
      steps: [{ name: 'research', goal: 'Research' }],
    },
  ]),
  executeRecipe: vi.fn(
    async (
      _recipe: unknown,
      _ctx: unknown,
      onProgress?: (step: string, status: string) => void
    ) => {
      onProgress?.('research', 'running')
      onProgress?.('research', 'completed')
      return {
        success: true,
        steps: [{ name: 'research', status: 'completed' }],
      }
    }
  ),
}))

describe('createBuiltinCommands', () => {
  const emit = vi.fn()
  const ctx = {
    sessionId: 'test-session',
    workingDirectory: '/tmp',
    signal: new AbortController().signal,
  }

  it('creates 13 built-in commands', () => {
    const commands = createBuiltinCommands(emit)
    expect(commands).toHaveLength(13)
  })

  it('includes expected command names', () => {
    const commands = createBuiltinCommands(emit)
    const names = commands.map((c) => c.name)
    expect(names).toContain('help')
    expect(names).toContain('clear')
    expect(names).toContain('mode')
    expect(names).toContain('architect')
    expect(names).toContain('model')
    expect(names).toContain('compact')
    expect(names).toContain('undo')
    expect(names).toContain('redo')
    expect(names).toContain('settings')
    expect(names).toContain('status')
    expect(names).toContain('recipe')
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

  it('/architect enables architect mode by default', async () => {
    const commands = createBuiltinCommands(emit)
    const architect = commands.find((c) => c.name === 'architect')!
    await architect.execute('', ctx)
    expect(emit).toHaveBeenCalledWith('mode:switch', { mode: 'architect' })
  })

  it('/architect off switches back to normal mode', async () => {
    const commands = createBuiltinCommands(emit)
    const architect = commands.find((c) => c.name === 'architect')!
    await architect.execute('off', ctx)
    expect(emit).toHaveBeenCalledWith('mode:switch', { mode: 'normal' })
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

  it('/recipe run emits progress events', async () => {
    const commands = createBuiltinCommands(emit)
    const recipe = commands.find((c) => c.name === 'recipe')!
    const message = await recipe.execute('run add-feature', ctx)

    expect(message).toContain('completed')
    expect(emit).toHaveBeenCalledWith('recipe:run-started', {
      sessionId: 'test-session',
      name: 'add-feature',
    })
    expect(emit).toHaveBeenCalledWith('recipe:progress', {
      sessionId: 'test-session',
      name: 'add-feature',
      step: 'research',
      status: 'running',
    })
  })
})
