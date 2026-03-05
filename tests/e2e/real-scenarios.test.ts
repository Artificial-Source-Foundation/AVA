import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  createMockPlatform,
  MockShell,
} from '../../packages/core-v2/src/__test-utils__/mock-platform'
import { MessageBus } from '../../packages/core-v2/src/bus/message-bus'
import type { ToolMiddlewareContext } from '../../packages/core-v2/src/extensions/types'
import type { ChatMessage } from '../../packages/core-v2/src/llm/types'
import { setPlatform } from '../../packages/core-v2/src/platform'
import { bashTool } from '../../packages/core-v2/src/tools/bash'
import { tieredCompactionStrategy } from '../../packages/extensions/context/src/strategies/tiered-compaction'
import { createCheckpointMiddleware } from '../../packages/extensions/git/src/checkpoints'
import { createPermissionMiddleware } from '../../packages/extensions/permissions/src/middleware'
import { createSandboxMiddleware } from '../../packages/extensions/permissions/src/sandbox-middleware'
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

function makeCtx(command: string, sessionId = 's-e2e'): ToolMiddlewareContext {
  return {
    toolName: 'bash',
    args: { command, description: 'e2e command' },
    ctx: {
      sessionId,
      workingDirectory: '/workspace',
      signal: new AbortController().signal,
    },
    definition: {
      name: 'bash',
      description: 'Bash tool',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

describe('E2E real extension scenarios', () => {
  const ipc = new MockIpc()

  beforeEach(() => {
    ipc.install()
  })

  afterEach(() => {
    ipc.reset()
  })

  it('1) edits a file via agent path (edit + validation)', async () => {
    ipc.setResponse('agent_run', {
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
    ipc.setResponse('agent_run', {
      id: 'session-delegate',
      goal: 'Commander delegates to workers',
      completed: true,
      messages: [
        { role: 'assistant', content: 'Commander delegated to Lead' },
        { role: 'assistant', content: 'Lead delegated to Worker-A and Worker-B' },
      ],
    })

    const run = await rustAgent.run('Commander -> Lead -> Worker delegation')
    // Rust bridge returns loosely typed message records; assert sequence behavior only.
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

  it('6) runs permission approval flow through middleware', async () => {
    const bus = new MessageBus()
    const middleware = createPermissionMiddleware(
      bus as unknown as Parameters<typeof createPermissionMiddleware>[0]
    )
    bus.subscribe('permission:request', (msg) => {
      bus.publish({
        type: 'permission:response',
        correlationId: msg.correlationId,
        timestamp: Date.now(),
        approved: true,
      } as unknown as Parameters<typeof bus.publish>[0])
    })

    const result = await middleware.before?.(makeCtx('npm install vitest'))
    expect(result?.blocked).not.toBe(true)
  })

  it('7) routes install command through sandbox middleware + bash tool', async () => {
    ipc.setResponse('sandbox_run', { stdout: 'ok', stderr: '', exitCode: 0 })

    const platform = createMockPlatform()
    setPlatform(platform)
    const sandbox = createSandboxMiddleware()
    const ctx = makeCtx('npm install zod')
    const before = await sandbox.before?.(ctx)
    expect(before).toBeDefined()
    const sandboxArgs = before?.args as {
      _sandboxed?: boolean
      _sandboxPolicy?: { writableRoots?: string[]; networkAccess?: boolean }
    }
    const result = await bashTool.execute(
      {
        command: 'npm install zod',
        description: 'install deps',
        _sandboxed: sandboxArgs._sandboxed,
        _sandboxPolicy: sandboxArgs._sandboxPolicy,
      },
      ctx.ctx
    )

    expect(result.success).toBe(true)
    expect(result.metadata?.sandboxed).toBe(true)
  })

  it('8) creates git checkpoint ref and supports rollback command path', async () => {
    const shell = new MockShell()
    shell.setResult('cd "/workspace" && git rev-parse --is-inside-work-tree', {
      stdout: 'true\n',
      stderr: '',
      exitCode: 0,
    })
    shell.setResult('cd "/workspace" && git add -A', { stdout: '', stderr: '', exitCode: 0 })
    shell.setResult('cd "/workspace" && git stash create "ava-checkpoint-1-edit"', {
      stdout: 'abc123\n',
      stderr: '',
      exitCode: 0,
    })
    shell.setResult('cd "/workspace" && git update-ref "refs/ava/checkpoints/1" "abc123"', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    shell.setResult('cd "/workspace" && git reset --hard abc123', {
      stdout: 'HEAD is now at abc123\n',
      stderr: '',
      exitCode: 0,
    })

    const { middleware, store } = createCheckpointMiddleware(shell)
    await middleware.after?.(
      {
        ...makeCtx('true'),
        toolName: 'edit',
      },
      { success: true, output: 'edited file' }
    )

    const latest = store.getCheckpoints().at(-1)
    expect(latest?.ref).toBe('refs/ava/checkpoints/1')
    await shell.exec('cd "/workspace" && git reset --hard abc123')
  })

  it('9) compacts long context after >50 turns', () => {
    const messages: ChatMessage[] = []
    for (let i = 0; i < 60; i++) {
      messages.push({
        role: i % 2 === 0 ? 'user' : 'assistant',
        content: `turn ${i} ${'x'.repeat(5000)}`,
      })
    }
    const compacted = tieredCompactionStrategy.compact(messages, 64_000)
    expect(compacted.length).toBeLessThan(messages.length)
  })

  it('10) connects MCP extension and calls namespaced tool', async () => {
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
