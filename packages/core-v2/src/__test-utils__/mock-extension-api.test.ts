import { describe, expect, it } from 'vitest'
import { createMockExtensionAPI } from './mock-extension-api.js'

describe('createMockExtensionAPI', () => {
  it('creates an API with all expected methods', () => {
    const { api } = createMockExtensionAPI()
    expect(api.registerTool).toBeTypeOf('function')
    expect(api.registerCommand).toBeTypeOf('function')
    expect(api.registerAgentMode).toBeTypeOf('function')
    expect(api.registerValidator).toBeTypeOf('function')
    expect(api.registerContextStrategy).toBeTypeOf('function')
    expect(api.registerProvider).toBeTypeOf('function')
    expect(api.addToolMiddleware).toBeTypeOf('function')
    expect(api.on).toBeTypeOf('function')
    expect(api.emit).toBeTypeOf('function')
    expect(api.bus).toBeDefined()
    expect(api.log).toBeDefined()
    expect(api.platform).toBeDefined()
    expect(api.storage).toBeDefined()
  })

  it('tracks tool registrations', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const tool = {
      definition: {
        name: 'test_tool',
        description: 'A test tool',
        input_schema: { type: 'object' as const, properties: {} },
      },
      execute: async () => ({ success: true, output: 'ok' }),
    }
    const disposable = api.registerTool(tool)
    expect(registeredTools).toHaveLength(1)
    expect(registeredTools[0].definition.name).toBe('test_tool')
    disposable.dispose()
    expect(registeredTools).toHaveLength(0)
  })

  it('tracks command registrations', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    const cmd = { name: 'test', description: 'test cmd', execute: async () => 'ok' }
    const disposable = api.registerCommand(cmd)
    expect(registeredCommands).toHaveLength(1)
    disposable.dispose()
    expect(registeredCommands).toHaveLength(0)
  })

  it('tracks provider registrations', () => {
    const { api, registeredProviders } = createMockExtensionAPI()
    const factory = () => ({ stream: async function* () {} }) as never
    const disposable = api.registerProvider('test-provider', factory)
    expect(registeredProviders).toHaveLength(1)
    expect(registeredProviders[0].name).toBe('test-provider')
    disposable.dispose()
    expect(registeredProviders).toHaveLength(0)
  })

  it('tracks emitted events', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    api.emit('test:event', { foo: 'bar' })
    expect(emittedEvents).toHaveLength(1)
    expect(emittedEvents[0]).toEqual({ event: 'test:event', data: { foo: 'bar' } })
  })

  it('delivers events to registered handlers', () => {
    const { api } = createMockExtensionAPI()
    let received: unknown = null
    api.on('my:event', (data) => {
      received = data
    })
    api.emit('my:event', 42)
    expect(received).toBe(42)
  })

  it('supports in-memory storage', async () => {
    const { api } = createMockExtensionAPI()
    await api.storage.set('key', 'value')
    expect(await api.storage.get('key')).toBe('value')
    expect(await api.storage.keys()).toEqual(['key'])
    await api.storage.delete('key')
    expect(await api.storage.get('key')).toBeNull()
  })

  it('cleans up all registrations on dispose', () => {
    const { api, registeredTools, registeredCommands, registeredProviders, dispose } =
      createMockExtensionAPI()
    api.registerTool({
      definition: { name: 't', description: '', input_schema: { type: 'object', properties: {} } },
      execute: async () => ({ success: true, output: '' }),
    })
    api.registerCommand({ name: 'c', description: '', execute: async () => '' })
    api.registerProvider('p', () => ({ stream: async function* () {} }) as never)
    expect(registeredTools).toHaveLength(1)
    expect(registeredCommands).toHaveLength(1)
    expect(registeredProviders).toHaveLength(1)
    dispose()
    expect(registeredTools).toHaveLength(0)
    expect(registeredCommands).toHaveLength(0)
    expect(registeredProviders).toHaveLength(0)
  })

  it('uses custom extension name', () => {
    const { api } = createMockExtensionAPI('my-ext')
    api.log.info('hello')
    expect(api.log.info).toHaveBeenCalledWith('hello')
  })
})
