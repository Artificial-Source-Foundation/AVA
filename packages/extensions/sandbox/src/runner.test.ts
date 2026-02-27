import { MockShell } from '@ava/core-v2/__test-utils__/mock-platform'
import { describe, expect, it } from 'vitest'
import { buildDockerCommand, isDockerAvailable, runInSandbox } from './runner.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

describe('buildDockerCommand', () => {
  it('builds basic docker run command', () => {
    const cmd = buildDockerCommand(DEFAULT_SANDBOX_CONFIG, 'echo hello')
    expect(cmd).toContain('docker run')
    expect(cmd).toContain('--rm')
    expect(cmd).toContain('--memory=512m')
    expect(cmd).toContain('--network=none')
    expect(cmd).toContain('node:22-slim')
    expect(cmd).toContain('echo hello')
  })

  it('enables network when configured', () => {
    const cmd = buildDockerCommand({ ...DEFAULT_SANDBOX_CONFIG, networkEnabled: true }, 'echo')
    expect(cmd).not.toContain('--network=none')
  })

  it('adds volume mounts', () => {
    const cmd = buildDockerCommand({ ...DEFAULT_SANDBOX_CONFIG, mountPaths: ['/data'] }, 'ls')
    expect(cmd).toContain('-v "/data:/data:ro"')
  })
})

describe('runInSandbox', () => {
  it('runs code and returns result', async () => {
    const shell = new MockShell()
    // Match the exact docker command
    shell.defaultResult = { stdout: 'hello\n', stderr: '', exitCode: 0 }

    const result = await runInSandbox(shell, DEFAULT_SANDBOX_CONFIG, 'echo hello')
    expect(result.stdout).toBe('hello\n')
    expect(result.exitCode).toBe(0)
    expect(result.timedOut).toBe(false)
    expect(result.durationMs).toBeGreaterThanOrEqual(0)
  })

  it('returns error result on failure', async () => {
    const shell = new MockShell()
    shell.defaultResult = { stdout: '', stderr: 'error', exitCode: 1 }

    const result = await runInSandbox(shell, DEFAULT_SANDBOX_CONFIG, 'bad cmd')
    expect(result.exitCode).toBe(1)
    expect(result.stderr).toBe('error')
  })
})

describe('isDockerAvailable', () => {
  it('returns true when docker is installed', async () => {
    const shell = new MockShell()
    shell.setResult('docker --version', {
      stdout: 'Docker version 24.0.0',
      stderr: '',
      exitCode: 0,
    })
    expect(await isDockerAvailable(shell)).toBe(true)
  })

  it('returns false when docker is not installed', async () => {
    const shell = new MockShell()
    shell.setResult('docker --version', {
      stdout: '',
      stderr: 'command not found',
      exitCode: 127,
    })
    expect(await isDockerAvailable(shell)).toBe(false)
  })
})
