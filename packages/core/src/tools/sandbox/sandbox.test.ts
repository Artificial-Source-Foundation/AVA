/**
 * Sandbox System Tests
 *
 * Tests for sandbox types, DockerSandbox argument building,
 * NoopSandbox interface, and factory function.
 * Actual Docker execution is not tested (requires Docker runtime).
 */

import { describe, expect, it } from 'vitest'
import { DockerSandbox } from './docker.js'
import { createSandbox } from './index.js'
import { NoopSandbox } from './noop.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

// ============================================================================
// DEFAULT_SANDBOX_CONFIG
// ============================================================================

describe('DEFAULT_SANDBOX_CONFIG', () => {
  it('defaults to none mode', () => {
    expect(DEFAULT_SANDBOX_CONFIG.mode).toBe('none')
  })

  it('uses node:20-slim image', () => {
    expect(DEFAULT_SANDBOX_CONFIG.image).toBe('node:20-slim')
  })

  it('has 120s timeout', () => {
    expect(DEFAULT_SANDBOX_CONFIG.timeoutSeconds).toBe(120)
  })

  it('disables network by default', () => {
    expect(DEFAULT_SANDBOX_CONFIG.networkAccess).toBe(false)
  })

  it('has 512m memory limit', () => {
    expect(DEFAULT_SANDBOX_CONFIG.memoryLimit).toBe('512m')
  })

  it('has 1 CPU limit', () => {
    expect(DEFAULT_SANDBOX_CONFIG.cpuLimit).toBe('1')
  })
})

// ============================================================================
// DockerSandbox
// ============================================================================

describe('DockerSandbox', () => {
  it('has type "docker"', () => {
    const sandbox = new DockerSandbox()
    expect(sandbox.type).toBe('docker')
  })

  it('builds correct docker args', () => {
    const sandbox = new DockerSandbox()
    const args = sandbox.buildDockerArgs('echo hello', '/project')

    expect(args).toContain('run')
    expect(args).toContain('--rm')
    expect(args).toContain('--network')
    expect(args).toContain('none')
    expect(args).toContain('-v')
    expect(args).toContain('/project:/workspace')
    expect(args).toContain('-w')
    expect(args).toContain('/workspace')
    expect(args).toContain('--memory')
    expect(args).toContain('512m')
    expect(args).toContain('--cpus')
    expect(args).toContain('1')
    expect(args).toContain('node:20-slim')
    expect(args).toContain('sh')
    expect(args).toContain('-c')
    expect(args).toContain('echo hello')
  })

  it('enables network when configured', () => {
    const sandbox = new DockerSandbox({ networkAccess: true })
    const args = sandbox.buildDockerArgs('curl http://example.com', '/project')

    expect(args).not.toContain('--network')
    expect(args).not.toContain('none')
  })

  it('uses custom image', () => {
    const sandbox = new DockerSandbox({ image: 'python:3.12' })
    const args = sandbox.buildDockerArgs('python script.py', '/project')

    expect(args).toContain('python:3.12')
    expect(args).not.toContain('node:20-slim')
  })

  it('uses custom memory limit', () => {
    const sandbox = new DockerSandbox({ memoryLimit: '1g' })
    const args = sandbox.buildDockerArgs('echo hi', '/project')

    const memIdx = args.indexOf('--memory')
    expect(args[memIdx + 1]).toBe('1g')
  })

  it('uses custom CPU limit', () => {
    const sandbox = new DockerSandbox({ cpuLimit: '2' })
    const args = sandbox.buildDockerArgs('echo hi', '/project')

    const cpuIdx = args.indexOf('--cpus')
    expect(args[cpuIdx + 1]).toBe('2')
  })

  it('getConfig returns copy', () => {
    const sandbox = new DockerSandbox({ image: 'alpine' })
    const config = sandbox.getConfig()
    expect(config.image).toBe('alpine')
    expect(config.mode).toBe('docker')

    // Modifying copy doesn't affect original
    config.image = 'ubuntu'
    expect(sandbox.getConfig().image).toBe('alpine')
  })

  it('mounts working directory as /workspace', () => {
    const sandbox = new DockerSandbox()
    const args = sandbox.buildDockerArgs('ls', '/home/user/code')

    const vIdx = args.indexOf('-v')
    expect(args[vIdx + 1]).toBe('/home/user/code:/workspace')

    const wIdx = args.indexOf('-w')
    expect(args[wIdx + 1]).toBe('/workspace')
  })

  it('cleanup is a no-op (uses --rm)', async () => {
    const sandbox = new DockerSandbox()
    // Should not throw
    await sandbox.cleanup()
  })
})

// ============================================================================
// NoopSandbox
// ============================================================================

describe('NoopSandbox', () => {
  it('has type "none"', () => {
    const sandbox = new NoopSandbox()
    expect(sandbox.type).toBe('none')
  })

  it('is always available', async () => {
    const sandbox = new NoopSandbox()
    expect(await sandbox.isAvailable()).toBe(true)
  })

  it('cleanup is a no-op', async () => {
    const sandbox = new NoopSandbox()
    await sandbox.cleanup() // Should not throw
  })
})

// ============================================================================
// createSandbox factory
// ============================================================================

describe('createSandbox', () => {
  it('creates NoopSandbox by default', () => {
    const sandbox = createSandbox()
    expect(sandbox.type).toBe('none')
    expect(sandbox).toBeInstanceOf(NoopSandbox)
  })

  it('creates NoopSandbox when mode is none', () => {
    const sandbox = createSandbox({ mode: 'none' })
    expect(sandbox.type).toBe('none')
  })

  it('creates DockerSandbox when mode is docker', () => {
    const sandbox = createSandbox({ mode: 'docker' })
    expect(sandbox.type).toBe('docker')
    expect(sandbox).toBeInstanceOf(DockerSandbox)
  })

  it('passes config to DockerSandbox', () => {
    const sandbox = createSandbox({
      mode: 'docker',
      image: 'alpine:3.19',
      networkAccess: true,
    }) as DockerSandbox

    const config = sandbox.getConfig()
    expect(config.image).toBe('alpine:3.19')
    expect(config.networkAccess).toBe(true)
  })
})

// ============================================================================
// Docker args — edge cases
// ============================================================================

describe('DockerSandbox — edge cases', () => {
  it('handles commands with special characters', () => {
    const sandbox = new DockerSandbox()
    const args = sandbox.buildDockerArgs('echo "hello world" && ls -la', '/project')
    expect(args).toContain('echo "hello world" && ls -la')
  })

  it('handles paths with spaces', () => {
    const sandbox = new DockerSandbox()
    const args = sandbox.buildDockerArgs('ls', '/my project')
    expect(args).toContain('/my project:/workspace')
  })

  it('args always end with sh -c command', () => {
    const sandbox = new DockerSandbox()
    const args = sandbox.buildDockerArgs('npm test', '/app')
    const lastThree = args.slice(-3)
    expect(lastThree).toEqual(['sh', '-c', 'npm test'])
  })
})
