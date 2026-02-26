import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import { bashTool } from './bash.js'
import type { ToolContext } from './types.js'

function makeCtx(cwd = '/project'): ToolContext {
  return {
    sessionId: 'test',
    workingDirectory: cwd,
    signal: new AbortController().signal,
  }
}

describe('bash tool', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
  })

  afterEach(() => {
    resetLogger()
  })

  it('executes command and returns output', async () => {
    platform.shell.setResult('bash -c echo hello', {
      stdout: 'hello\n',
      stderr: '',
      exitCode: 0,
    })

    const result = await bashTool.execute(
      { command: 'echo hello', description: 'Print hello' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('hello')
  })

  it('handles non-zero exit code', async () => {
    platform.shell.setResult('bash -c false', {
      stdout: '',
      stderr: 'error occurred',
      exitCode: 1,
    })

    const result = await bashTool.execute(
      { command: 'false', description: 'Fail command' },
      makeCtx()
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('Exit code: 1')
  })

  it('includes stderr in error output', async () => {
    platform.shell.setResult('bash -c bad', {
      stdout: '',
      stderr: 'bad: command not found',
      exitCode: 127,
    })

    const result = await bashTool.execute({ command: 'bad', description: 'Bad command' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.output).toContain('command not found')
  })

  it('uses specified working directory', async () => {
    platform.shell.setResult('bash -c pwd', {
      stdout: '/other/dir\n',
      stderr: '',
      exitCode: 0,
    })

    const result = await bashTool.execute(
      { command: 'pwd', description: 'Print cwd', workdir: '/other/dir' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.metadata).toBeDefined()
    expect(result.metadata!.cwd).toBe('/other/dir')
  })

  it('returns metadata', async () => {
    platform.shell.setResult('bash -c echo hi', {
      stdout: 'hi\n',
      stderr: '',
      exitCode: 0,
    })

    const result = await bashTool.execute({ command: 'echo hi', description: 'Test' }, makeCtx())
    expect(result.metadata!.command).toBe('echo hi')
    expect(result.metadata!.exitCode).toBe(0)
    expect(result.metadata!.description).toBe('Test')
  })

  it('throws on abort', async () => {
    const controller = new AbortController()
    controller.abort()
    await expect(
      bashTool.execute(
        { command: 'echo hello', description: 'Test' },
        { sessionId: 'test', workingDirectory: '/project', signal: controller.signal }
      )
    ).rejects.toThrow('Aborted')
  })

  it('returns locations', async () => {
    platform.shell.setResult('bash -c echo hi', {
      stdout: 'hi\n',
      stderr: '',
      exitCode: 0,
    })

    const result = await bashTool.execute({ command: 'echo hi', description: 'Test' }, makeCtx())
    expect(result.locations).toBeDefined()
    expect(result.locations![0].type).toBe('exec')
  })
})
