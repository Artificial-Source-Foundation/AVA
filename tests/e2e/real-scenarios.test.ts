/**
 * E2E real extension scenarios
 *
 * These tests previously exercised the TS orchestration layer (packages/core-v2, packages/extensions).
 * That layer has been removed — all orchestration now happens in Rust.
 * The scenarios below test the Rust IPC bridge wrappers via MockIpc.
 */

import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  rustAgent,
  rustCompute,
  rustExtensions,
  rustMemory,
  rustPlugins,
  rustTools,
} from '../../src/services/rust-bridge'
import { AppHarness } from './helpers/app-harness'
import { MockIpc } from './helpers/mock-ipc'

describe('E2E real extension scenarios', () => {
  const ipc = new MockIpc()

  beforeEach(() => {
    ipc.install()
  })

  afterEach(() => {
    ipc.reset()
  })

  it('1) edits a file via agent path (edit + validation)', async () => {
    ipc.setResponse('submit_goal', {
      id: 'session-edit',
      goal: 'update file',
      completed: true,
      messages: [{ role: 'assistant', content: 'edited and validated' }],
    })
    ipc.setHandler('execute_tool', (input) => {
      const { tool } = input as { tool: string }
      if (tool !== 'edit') return { content: 'unexpected tool', is_error: true }
      return { content: 'edit applied', is_error: false }
    })
    ipc.setResponse('compute_fuzzy_replace', {
      content: 'const x=2',
      strategy: 'exact',
    })
    ipc.setResponse('validation_validate_edit', { valid: true, details: [] })

    const harness = new AppHarness({ mock: false })
    const run = await harness.runAgent('Edit src/app.ts and validate')
    const editResult = await harness.executeTool('edit', {
      filePath: '/workspace/src/app.ts',
      oldString: 'old',
      newString: 'new',
    })
    const validation = await rustCompute.fuzzyReplace('const x=1', '1', '2')

    expect(run.completed).toBe(true)
    expect(editResult.is_error).toBe(false)
    expect(validation.strategy).toBeDefined()
  })

  it('2) searches codebase via Rust grep compute', async () => {
    ipc.setResponse('compute_grep', {
      matches: [{ file: 'src/a.ts', line: 1, content: 'const hello = 1' }],
      truncated: false,
    })

    const result = await rustCompute.grep('/workspace/src', 'hello')
    expect(result.matches).toHaveLength(1)
    expect(result.matches[0]?.file).toBe('src/a.ts')
  })

  it('3) handles multi-agent delegation output', async () => {
    ipc.setResponse('submit_goal', {
      id: 'session-delegate',
      goal: 'Commander delegates to workers',
      completed: true,
      messages: [
        { role: 'assistant', content: 'Commander delegated to Lead' },
        { role: 'assistant', content: 'Lead delegated to Worker-A and Worker-B' },
      ],
    })

    const run = await rustAgent.run('Commander -> Lead -> Worker delegation')
    expect(Array.isArray(run.messages)).toBe(true)
    expect(run.completed).toBe(true)
    expect(run.messages.length).toBeGreaterThan(1)
  })

  it('4) supports plugin load/unload lifecycle', async () => {
    ipc.setResponse('install_plugin', { id: 'demo.plugin', enabled: true, installed: true })
    ipc.setResponse('uninstall_plugin', { id: 'demo.plugin', enabled: false, installed: false })

    const installed = await rustPlugins.install('demo.plugin')
    const removed = await rustPlugins.uninstall('demo.plugin')

    expect(installed.installed).toBe(true)
    expect(removed.installed).toBe(false)
  })

  it('5) persists memory across sessions', async () => {
    const memory = new Map<string, string>()
    ipc.setHandler('memory_remember', (input) => {
      const { key, value } = input as { key: string; value: string }
      memory.set(key, value)
      return { id: 1, key, value, createdAt: new Date().toISOString() }
    })
    ipc.setHandler('memory_recall', (input) => {
      const { key } = input as { key: string }
      const value = memory.get(key)
      if (!value) return null
      return { id: 1, key, value, createdAt: new Date().toISOString() }
    })

    await rustMemory.remember('session-note', 'persist me')
    const recalled = await rustMemory.recall('session-note')

    expect(recalled?.value).toBe('persist me')
  })

  it('6) connects MCP extension and calls namespaced tool', async () => {
    ipc.setResponse('extensions_register_native', {
      kind: 'native',
      name: 'demo-mcp',
      version: '1.0.0',
      path: '/tmp/demo.so',
      tools: ['demo:echo'],
      hooks: [],
      validators: [],
    })
    ipc.setResponse('execute_tool', { content: 'echo:hello', is_error: false })

    const registration = await rustExtensions.registerNative({
      name: 'demo-mcp',
      version: '1.0.0',
      path: '/tmp/demo.so',
      tools: ['demo:echo'],
      hooks: [],
      validators: [],
    })
    const toolResult = await rustTools.execute('demo:echo', { text: 'hello' })

    expect(registration.tools).toContain('demo:echo')
    expect(toolResult.content).toContain('hello')
  })
})
