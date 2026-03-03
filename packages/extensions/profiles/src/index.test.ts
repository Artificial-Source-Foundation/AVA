import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { createMockExtensionAPI } from '../../../core-v2/src/__test-utils__/mock-extension-api.js'
import { activate } from './index.js'

let tempHome = ''
let originalHome = ''

beforeEach(async () => {
  originalHome = process.env.HOME ?? ''
  tempHome = await mkdtemp(join(tmpdir(), 'ava-profiles-'))
  process.env.HOME = tempHome
})

afterEach(async () => {
  process.env.HOME = originalHome
  if (tempHome) {
    await rm(tempHome, { recursive: true, force: true })
  }
})

function toolCtx() {
  return {
    sessionId: 's',
    workingDirectory: '/tmp',
    signal: new AbortController().signal,
  }
}

function installHookSupport(api: ReturnType<typeof createMockExtensionAPI>['api']): void {
  const handlers = new Map<string, Array<(payload: unknown) => Promise<unknown> | unknown>>()

  ;(api as unknown as { registerHook: ExtensionHookRegistrar['registerHook'] }).registerHook = (
    event,
    callback
  ) => {
    const list = handlers.get(event) ?? []
    list.push(callback as (payload: unknown) => Promise<unknown> | unknown)
    handlers.set(event, list)
    return { dispose() {} }
  }

  ;(api as unknown as { callHook: ExtensionHookRegistrar['callHook'] }).callHook = async (
    event,
    payload,
    defaultValue
  ) => {
    const list = handlers.get(event) ?? []
    let current: unknown = payload
    for (const callback of list) {
      const next = await callback(current)
      if (next !== undefined) {
        current = next
      }
    }
    return (current ?? defaultValue) as never
  }
}

interface ExtensionHookRegistrar {
  registerHook: (event: string, callback: (payload: unknown) => unknown) => { dispose(): void }
  callHook: (event: string, payload: unknown, defaultValue: unknown) => Promise<unknown>
}

describe('profiles extension', () => {
  it('supports save/load/list profile tools', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    installHookSupport(api)
    activate(api)

    const save = registeredTools.find((tool) => tool.definition.name === 'profile_save')
    const list = registeredTools.find((tool) => tool.definition.name === 'profile_list')
    const load = registeredTools.find((tool) => tool.definition.name === 'profile_load')

    expect(save).toBeDefined()
    expect(list).toBeDefined()
    expect(load).toBeDefined()

    const saved = await save!.execute(
      {
        name: 'custom',
        model: 'gpt-4o',
        tools: ['read_file'],
        instructions: 'Only read files',
        skills: ['researcher'],
      },
      toolCtx()
    )
    expect(saved.success).toBe(true)

    const listed = await list!.execute({}, toolCtx())
    expect(listed.success).toBe(true)
    const names = listed.metadata?.names as string[]
    expect(names).toContain('custom')

    const loaded = await load!.execute({ name: 'custom' }, toolCtx())
    expect(loaded.success).toBe(true)
    const profile = loaded.metadata?.profile as { name: string; model: string }
    expect(profile.name).toBe('custom')
    expect(profile.model).toBe('gpt-4o')
  })

  it('applies active profile to middleware and history hook', async () => {
    const { api, registeredTools, registeredMiddleware } = createMockExtensionAPI()
    installHookSupport(api)
    activate(api)

    const load = registeredTools.find((tool) => tool.definition.name === 'profile_load')
    expect(load).toBeDefined()
    await load!.execute({ name: 'reviewer' }, toolCtx())

    const middleware = registeredMiddleware.find((entry) => entry.name === 'profile-tool-filter')
    expect(middleware).toBeDefined()

    const blocked = await middleware!.before?.({
      toolName: 'edit',
      args: {},
      ctx: {
        sessionId: 's',
        workingDirectory: '/tmp',
        signal: new AbortController().signal,
      },
      definition: {
        name: 'edit',
        description: '',
        input_schema: { type: 'object', properties: {} },
      },
    })
    expect(blocked?.blocked).toBe(true)

    const processed = await api.callHook(
      'history:process',
      [{ role: 'user', content: 'hello' }],
      []
    )
    expect(Array.isArray(processed)).toBe(true)
    const first = (processed as unknown as Array<{ role: string; content: string }>)[0]
    expect(first.role).toBe('system')
    expect(first.content).toContain('[Active profile: reviewer]')
  })
})
