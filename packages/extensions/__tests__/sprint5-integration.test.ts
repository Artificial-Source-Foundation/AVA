/**
 * Sprint 5 integration test — ecosystem extensions + provider coexistence.
 *
 * Verifies:
 * 1. All 14 providers register without conflict
 * 2. Extension lifecycle (activate/dispose) across multiple extensions
 * 3. Extension API provides isolated storage per extension
 * 4. Agent modes from multiple extensions coexist
 * 5. Event system handles cross-extension communication
 */

import { AgentExecutor } from '@ava/core-v2/agent'
import { MessageBus } from '@ava/core-v2/bus'
import { createExtensionAPI, getAgentModes, resetRegistries } from '@ava/core-v2/extensions'
import type { LLMProvider } from '@ava/core-v2/llm'
import { registerProvider, resetProviders } from '@ava/core-v2/llm'
import type { IPlatformProvider } from '@ava/core-v2/platform'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager } from '@ava/core-v2/session'
import { resetTools } from '@ava/core-v2/tools'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'

import { planAgentMode, resetPlanMode } from '../agent-modes/src/plan-mode.js'
import { activate as activateCommander } from '../commander/src/index.js'

// ─── Test Helpers ──────────────────────────────────────────────────────────

function installMockPlatform() {
  const platform = {
    fs: {
      async readFile() {
        return ''
      },
      async writeFile() {},
      async readDir() {
        return []
      },
      async exists() {
        return false
      },
      async stat() {
        return { isFile: false, isDirectory: true, size: 0, mtime: Date.now() }
      },
      async isFile() {
        return false
      },
      async isDirectory() {
        return true
      },
      async mkdir() {},
      async remove() {},
      async glob() {
        return []
      },
      async readBinary() {
        return new Uint8Array()
      },
      async writeBinary() {},
      async readDirWithTypes() {
        return []
      },
      async realpath(path: string) {
        return path
      },
    },
    shell: {
      async exec() {
        return { stdout: '', stderr: '', exitCode: 0 }
      },
      spawn() {
        return {}
      },
    },
    credentials: {
      async get() {
        return null
      },
      async set() {},
      async delete() {},
      async has() {
        return false
      },
    },
    database: {
      async query() {
        return []
      },
      async execute() {},
      async migrate() {},
      async close() {},
    },
  }
  setPlatform(platform as unknown as IPlatformProvider)
  return platform
}

function makeExtApi(name = 'test') {
  return createExtensionAPI(name, new MessageBus(), createSessionManager())
}

// ─── Tests ─────────────────────────────────────────────────────────────────

describe('Sprint 5 Integration', () => {
  beforeEach(() => {
    resetTools()
    resetProviders()
    resetRegistries()
    resetPlanMode()
    installMockPlatform()
  })

  afterEach(() => {
    resetTools()
    resetProviders()
    resetRegistries()
    resetPlanMode()
  })

  describe('Provider coexistence', () => {
    it('registers all 14 providers without conflict', () => {
      const providers = [
        'anthropic',
        'openai',
        'openrouter',
        'google',
        'deepseek',
        'groq',
        'mistral',
        'cohere',
        'together',
        'xai',
        'ollama',
        'glm',
        'kimi',
        'copilot',
      ]

      for (const name of providers) {
        registerProvider(name, () => ({
          async *stream() {
            yield { content: `Hello from ${name}`, done: true }
          },
        }))
      }

      // All providers should work independently
      for (const name of providers) {
        const agent = new AgentExecutor({
          provider: name as LLMProvider,
          maxTurns: 1,
          maxTimeMinutes: 1,
        })
        expect(agent.config.provider).toBe(name)
      }
    })

    it('ExtensionAPI registerProvider returns disposable', () => {
      const api = makeExtApi()

      const d1 = api.registerProvider('test-a', () => ({
        async *stream() {
          yield { content: 'a', done: true }
        },
      }))
      const d2 = api.registerProvider('test-b', () => ({
        async *stream() {
          yield { content: 'b', done: true }
        },
      }))

      // Both work
      expect(
        () =>
          new AgentExecutor({ provider: 'test-a' as LLMProvider, maxTurns: 1, maxTimeMinutes: 1 })
      ).not.toThrow()
      expect(
        () =>
          new AgentExecutor({ provider: 'test-b' as LLMProvider, maxTurns: 1, maxTimeMinutes: 1 })
      ).not.toThrow()

      // Dispose one
      d1.dispose()
      d2.dispose()
    })
  })

  describe('Extension lifecycle', () => {
    it('multiple extensions activate and dispose independently', () => {
      const api = makeExtApi()
      const disposables: { dispose: () => void }[] = []

      // Activate plan mode
      disposables.push(api.registerAgentMode(planAgentMode))

      // Activate commander
      disposables.push(activateCommander(api))

      expect(getAgentModes().has('plan')).toBe(true)
      expect(getAgentModes().has('team')).toBe(true)

      // Dispose first
      disposables[0]!.dispose()
      expect(getAgentModes().has('plan')).toBe(false)
      expect(getAgentModes().has('team')).toBe(true)

      // Dispose second
      disposables[1]!.dispose()
      expect(getAgentModes().has('team')).toBe(false)
    })

    it('extension API provides isolated storage per extension', async () => {
      const apiA = makeExtApi('ext-a')
      const apiB = makeExtApi('ext-b')

      // Each extension writes to its own storage
      await apiA.storage.set('key', 'value-a')
      await apiB.storage.set('key', 'value-b')

      expect(await apiA.storage.get('key')).toBe('value-a')
      expect(await apiB.storage.get('key')).toBe('value-b')

      // Delete in one doesn't affect the other
      await apiA.storage.delete('key')
      expect(await apiA.storage.get('key')).toBeNull()
      expect(await apiB.storage.get('key')).toBe('value-b')
    })

    it('extension storage supports keys listing', async () => {
      const api = makeExtApi()

      await api.storage.set('alpha', 1)
      await api.storage.set('beta', 2)
      await api.storage.set('gamma', 3)

      const keys = await api.storage.keys()
      expect(keys).toHaveLength(3)
      expect(keys.sort()).toEqual(['alpha', 'beta', 'gamma'])
    })
  })

  describe('Cross-extension event communication', () => {
    it('events emitted by one extension are received by another', () => {
      const apiA = makeExtApi('ext-a')
      const apiB = makeExtApi('ext-b')
      const received: unknown[] = []

      // B subscribes to events
      apiB.on('custom:notification', (data) => received.push(data))

      // A emits an event
      apiA.emit('custom:notification', { message: 'hello from A' })

      expect(received).toHaveLength(1)
      expect((received[0] as { message: string }).message).toBe('hello from A')
    })

    it('event handler disposal stops receiving events', () => {
      const api = makeExtApi()
      const received: unknown[] = []

      const disposable = api.on('test:event', (data) => received.push(data))

      api.emit('test:event', { n: 1 })
      expect(received).toHaveLength(1)

      disposable.dispose()

      api.emit('test:event', { n: 2 })
      expect(received).toHaveLength(1) // no new event received
    })

    it('multiple handlers for the same event all fire', () => {
      const api = makeExtApi()
      const log: string[] = []

      api.on('shared:event', () => log.push('handler-1'))
      api.on('shared:event', () => log.push('handler-2'))
      api.on('shared:event', () => log.push('handler-3'))

      api.emit('shared:event', {})

      expect(log).toEqual(['handler-1', 'handler-2', 'handler-3'])
    })
  })

  describe('Extension manifest validation', () => {
    it('ecosystem extensions have correct manifest structure', async () => {
      const manifests = [
        '../mcp/ava-extension.json',
        '../codebase/ava-extension.json',
        '../git/ava-extension.json',
        '../lsp/ava-extension.json',
        '../diff/ava-extension.json',
        '../focus-chain/ava-extension.json',
        '../instructions/ava-extension.json',
        '../models/ava-extension.json',
        '../scheduler/ava-extension.json',
        '../sandbox/ava-extension.json',
        '../skills/ava-extension.json',
        '../custom-commands/ava-extension.json',
        '../slash-commands/ava-extension.json',
        '../integrations/ava-extension.json',
      ]

      for (const path of manifests) {
        const manifest = await import(path)
        expect(manifest.name).toBeDefined()
        expect(manifest.version).toBe('1.0.0')
        expect(manifest.builtIn).toBe(true)
        expect(typeof manifest.priority).toBe('number')
        expect(typeof manifest.enabledByDefault).toBe('boolean')
      }
    })
  })

  describe('Full ecosystem: agent with multiple extensions', () => {
    it('agent uses team mode from commander with providers', async () => {
      const api = makeExtApi()

      // Activate commander
      const cmdDisposable = activateCommander(api)
      expect(getAgentModes().has('team')).toBe(true)

      // Register mock provider
      registerProvider('mock', () => ({
        async *stream() {
          yield { content: 'Delegating to Coder...' }
          yield { done: true }
        },
      }))

      const agent = new AgentExecutor({
        provider: 'mock' as LLMProvider,
        maxTurns: 1,
        maxTimeMinutes: 1,
        toolMode: 'team',
      })

      const result = await agent.run(
        { goal: 'Build a feature', cwd: '/tmp' },
        AbortSignal.timeout(5000)
      )

      expect(result.success).toBe(true)
      expect(result.output).toContain('Delegating')

      cmdDisposable.dispose()
    })
  })
})
