/**
 * Git extension integration test — activates the full extension, simulates
 * session lifecycle events, and verifies snapshot middleware + cleanup.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import type { MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

function setupGitRepo(platform: MockPlatform, cwd: string): void {
  platform.shell.setResult(`git -C "${cwd}" rev-parse --is-inside-work-tree`, {
    stdout: 'true\n',
    stderr: '',
    exitCode: 0,
  })
}

function setupNonGitDir(platform: MockPlatform, cwd: string): void {
  platform.shell.setResult(`git -C "${cwd}" rev-parse --is-inside-work-tree`, {
    stdout: '',
    stderr: 'fatal: not a git repository',
    exitCode: 128,
  })
}

describe('git extension integration', () => {
  it('registers snapshot middleware with correct priority on activate', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)

    const snapshotMw = registeredMiddleware.find((m) => m.name === 'ava-git-snapshots')
    expect(snapshotMw).toBeDefined()
    expect(snapshotMw!.priority).toBe(30)
  })

  it('registers checkpoint middleware with correct priority on activate', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)

    const checkpointMw = registeredMiddleware.find((m) => m.name === 'ava-checkpoints')
    expect(checkpointMw).toBeDefined()
    expect(checkpointMw!.priority).toBe(20)
  })

  it('sets up git context and emits git:ready after session:opened in a git repo', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const cwd = '/my/project'
    setupGitRepo(api.platform as MockPlatform, cwd)

    activate(api)
    api.emit('session:opened', { sessionId: 's1', workingDirectory: cwd })

    // Wait for the async isGitRepo check
    await new Promise((r) => setTimeout(r, 50))

    const readyEvent = emittedEvents.find((e) => e.event === 'git:ready')
    expect(readyEvent).toBeDefined()
    expect((readyEvent!.data as Record<string, unknown>).cwd).toBe(cwd)
  })

  it('does not emit git:ready when not in a git repo', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const cwd = '/tmp/not-a-repo'
    setupNonGitDir(api.platform as MockPlatform, cwd)

    activate(api)
    api.emit('session:opened', { sessionId: 's2', workingDirectory: cwd })

    await new Promise((r) => setTimeout(r, 50))

    const readyEvent = emittedEvents.find((e) => e.event === 'git:ready')
    expect(readyEvent).toBeUndefined()
  })

  it('snapshot middleware takes snapshots before file-write tools in git repos', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    const cwd = '/my/project'
    setupGitRepo(api.platform as MockPlatform, cwd)

    const platform = api.platform as MockPlatform
    platform.shell.setResult(
      `git -C "${cwd}" stash create "Before write_file: /my/project/file.ts"`,
      {
        stdout: 'abc123\n',
        stderr: '',
        exitCode: 0,
      }
    )

    activate(api)
    api.emit('session:opened', { sessionId: 's3', workingDirectory: cwd })
    await new Promise((r) => setTimeout(r, 50))

    const snapshotMw = registeredMiddleware.find((m) => m.name === 'ava-git-snapshots')
    expect(snapshotMw).toBeDefined()
    expect(snapshotMw!.before).toBeDefined()

    // Simulate a file-write tool call through the middleware
    const result = await snapshotMw!.before!({
      toolName: 'write_file',
      args: { path: '/my/project/file.ts', content: 'new content' },
      ctx: { sessionId: 's3', workingDirectory: cwd, signal: new AbortController().signal },
      definition: {
        name: 'write_file',
        description: '',
        input_schema: { type: 'object', properties: {} },
      },
    })

    // Snapshot middleware returns undefined (it doesn't block)
    expect(result).toBeUndefined()
  })

  it('snapshot middleware ignores non-file-write tools', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    const cwd = '/my/project'
    setupGitRepo(api.platform as MockPlatform, cwd)

    activate(api)
    api.emit('session:opened', { sessionId: 's4', workingDirectory: cwd })
    await new Promise((r) => setTimeout(r, 50))

    const snapshotMw = registeredMiddleware.find((m) => m.name === 'ava-git-snapshots')
    expect(snapshotMw).toBeDefined()

    const result = await snapshotMw!.before!({
      toolName: 'grep',
      args: { pattern: 'test' },
      ctx: { sessionId: 's4', workingDirectory: cwd, signal: new AbortController().signal },
      definition: {
        name: 'grep',
        description: '',
        input_schema: { type: 'object', properties: {} },
      },
    })

    expect(result).toBeUndefined()
  })

  it('registers all 4 git tools', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)

    const toolNames = registeredTools.map((t) => t.definition.name)
    expect(toolNames).toContain('create_pr')
    expect(toolNames).toContain('create_branch')
    expect(toolNames).toContain('switch_branch')
    expect(toolNames).toContain('read_issue')
  })

  it('registers /snapshot and /undo commands', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    const names = registeredCommands.map((c) => c.name)
    expect(names).toContain('snapshot')
    expect(names).toContain('undo')
  })

  it('cleans up all registrations on dispose', () => {
    const { api, registeredTools, registeredMiddleware, registeredCommands, eventHandlers } =
      createMockExtensionAPI()

    const disposable = activate(api)

    // Verify things were registered
    expect(registeredTools.length).toBeGreaterThan(0)
    expect(registeredMiddleware.length).toBeGreaterThan(0)
    expect(registeredCommands.length).toBeGreaterThan(0)
    expect(eventHandlers.has('session:opened')).toBe(true)

    disposable.dispose()

    expect(registeredTools).toHaveLength(0)
    expect(registeredMiddleware).toHaveLength(0)
    expect(registeredCommands).toHaveLength(0)
    expect(eventHandlers.has('session:opened')).toBe(false)
  })

  it('handles multiple session:opened events gracefully', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const platform = api.platform as MockPlatform

    setupGitRepo(platform, '/project-a')
    setupGitRepo(platform, '/project-b')

    activate(api)

    api.emit('session:opened', { sessionId: 's5', workingDirectory: '/project-a' })
    await new Promise((r) => setTimeout(r, 50))

    api.emit('session:opened', { sessionId: 's6', workingDirectory: '/project-b' })
    await new Promise((r) => setTimeout(r, 50))

    const readyEvents = emittedEvents.filter((e) => e.event === 'git:ready')
    expect(readyEvents).toHaveLength(2)
  })
})
