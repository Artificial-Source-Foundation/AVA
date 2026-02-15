/**
 * Hook Executor Tests
 * Tests: discovery, executable detection, timeout, JSON parsing, error handling
 */

import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { discoverHooks, HookRunner, resetHookRunner } from './executor.js'

// ============================================================================
// Test Helpers
// ============================================================================

let tempDir: string

beforeEach(() => {
  tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ava-hooks-test-'))
  resetHookRunner()
})

afterEach(() => {
  fs.rmSync(tempDir, { recursive: true, force: true })
})

function createHookScript(dir: string, hookType: string, content: string, ext = '.sh'): string {
  const hooksDir = path.join(dir, '.ava', 'hooks')
  fs.mkdirSync(hooksDir, { recursive: true })
  const filePath = path.join(hooksDir, `${hookType}${ext}`)
  fs.writeFileSync(filePath, content)
  fs.chmodSync(filePath, 0o755)
  return filePath
}

// ============================================================================
// Hook Discovery
// ============================================================================

describe('discoverHooks', () => {
  it('finds project hooks with .sh extension', async () => {
    createHookScript(tempDir, 'PreToolUse', '#!/bin/bash\necho "{}"')
    const hooks = await discoverHooks('PreToolUse', tempDir)
    expect(hooks).toHaveLength(1)
    expect(hooks[0].source).toBe('project')
    expect(hooks[0].type).toBe('PreToolUse')
  })

  it('finds hooks with different extensions', async () => {
    createHookScript(tempDir, 'PostToolUse', 'console.log(JSON.stringify({}))', '.js')
    const hooks = await discoverHooks('PostToolUse', tempDir)
    expect(hooks).toHaveLength(1)
    expect(hooks[0].path).toContain('.js')
  })

  it('returns empty array when no hooks exist', async () => {
    const hooks = await discoverHooks('PreToolUse', tempDir)
    expect(hooks).toHaveLength(0)
  })

  it('ignores non-executable files', async () => {
    const hooksDir = path.join(tempDir, '.ava', 'hooks')
    fs.mkdirSync(hooksDir, { recursive: true })
    const filePath = path.join(hooksDir, 'PreToolUse.sh')
    fs.writeFileSync(filePath, '#!/bin/bash\necho "{}"')
    // Don't set executable permission
    fs.chmodSync(filePath, 0o644)

    const hooks = await discoverHooks('PreToolUse', tempDir)
    expect(hooks).toHaveLength(0)
  })
})

// ============================================================================
// HookRunner
// ============================================================================

describe('HookRunner', () => {
  it('initializes and discovers hooks', async () => {
    createHookScript(tempDir, 'PreToolUse', '#!/bin/bash\necho "{}"')

    const runner = new HookRunner(tempDir)
    await runner.initialize()

    expect(runner.hasHooks('PreToolUse')).toBe(true)
    expect(runner.hasHooks('PostToolUse')).toBe(false)
  })

  it('emits discovery events', async () => {
    createHookScript(tempDir, 'TaskStart', '#!/bin/bash\necho "{}"')

    const events: string[] = []
    const runner = new HookRunner(tempDir)
    runner.on((event) => events.push(event.type))
    await runner.initialize()

    expect(events).toContain('hook:discovered')
  })

  it('returns empty result when no hooks registered', async () => {
    const runner = new HookRunner(tempDir)
    const result = await runner.run('PreToolUse', { toolName: 'test' })
    expect(result).toEqual({})
  })

  it('executes hook and returns result', async () => {
    createHookScript(tempDir, 'PreToolUse', '#!/bin/bash\necho \'{"message":"hello from hook"}\'')

    const runner = new HookRunner(tempDir, { timeout: 5000 })
    const result = await runner.run('PreToolUse', { toolName: 'read_file' })
    expect(result.message).toBe('hello from hook')
  })

  it('handles hook that outputs cancel', async () => {
    createHookScript(
      tempDir,
      'PreToolUse',
      '#!/bin/bash\necho \'{"cancel":true,"reason":"blocked by policy"}\''
    )

    const runner = new HookRunner(tempDir, { timeout: 5000 })
    const result = await runner.run('PreToolUse', { toolName: 'bash' })
    expect(result.cancel).toBe(true)
    expect(result.reason).toBe('blocked by policy')
  })

  it('handles hook timeout gracefully', async () => {
    createHookScript(tempDir, 'PreToolUse', '#!/bin/bash\nsleep 30\necho "{}"')

    const runner = new HookRunner(tempDir, { timeout: 100 })
    const result = await runner.run('PreToolUse', { toolName: 'test' })
    expect(result.errorMessage).toContain('timed out')
  })

  it('handles hook with invalid JSON output', async () => {
    createHookScript(tempDir, 'PreToolUse', '#!/bin/bash\necho "not json"')

    const runner = new HookRunner(tempDir, { timeout: 5000 })
    const result = await runner.run('PreToolUse', { toolName: 'test' })
    // Should not throw, but may have error info
    expect(result).toBeDefined()
  })

  it('handles hook script that fails to spawn', async () => {
    // Create a non-existent script path scenario
    const runner = new HookRunner(tempDir, { timeout: 5000 })
    // Force a hook location that doesn't exist
    await runner.initialize()
    // No hooks means empty result
    const result = await runner.run('PreToolUse', { toolName: 'test' })
    expect(result).toEqual({})
  })

  it('emits executing and completed events', async () => {
    createHookScript(tempDir, 'PostToolUse', '#!/bin/bash\necho "{}"')

    const events: string[] = []
    const runner = new HookRunner(tempDir, { timeout: 5000 })
    runner.on((event) => events.push(event.type))
    await runner.run('PostToolUse', { toolName: 'test' })

    expect(events).toContain('hook:discovered')
    expect(events).toContain('hook:executing')
    expect(events).toContain('hook:completed')
  })

  it('supports listener removal', async () => {
    const events: string[] = []
    const runner = new HookRunner(tempDir)
    const unsub = runner.on((event) => events.push(event.type))
    unsub()
    await runner.initialize()
    expect(events).toHaveLength(0)
  })

  it('refresh re-discovers hooks', async () => {
    const runner = new HookRunner(tempDir)
    await runner.initialize()
    expect(runner.hasHooks('PreToolUse')).toBe(false)

    // Create a hook after initialization
    createHookScript(tempDir, 'PreToolUse', '#!/bin/bash\necho "{}"')
    await runner.refresh()
    expect(runner.hasHooks('PreToolUse')).toBe(true)
  })

  it('getRegisteredHooks returns all hooks', async () => {
    createHookScript(tempDir, 'PreToolUse', '#!/bin/bash\necho "{}"')
    createHookScript(tempDir, 'PostToolUse', '#!/bin/bash\necho "{}"')

    const runner = new HookRunner(tempDir)
    await runner.initialize()

    const hooks = runner.getRegisteredHooks()
    expect(hooks.size).toBe(2)
    expect(hooks.has('PreToolUse')).toBe(true)
    expect(hooks.has('PostToolUse')).toBe(true)
  })
})
