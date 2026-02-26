/**
 * ls tool — directory listing.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { lsTool } from './ls.js'

let platform: MockPlatform

function makeCtx() {
  return {
    sessionId: 'test',
    workingDirectory: '/project',
    signal: AbortSignal.timeout(5000),
  }
}

beforeEach(() => {
  platform = installMockPlatform()
})

afterEach(() => {
  resetLogger()
})

describe('lsTool', () => {
  it('has correct name', () => {
    expect(lsTool.definition.name).toBe('ls')
  })

  it('lists directory contents', async () => {
    platform.fs.addFile('/project/src/index.ts', 'export {}')
    platform.fs.addFile('/project/package.json', '{}')
    platform.fs.addFile('/project/README.md', '# Hello')

    const result = await lsTool.execute({ path: '/project' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('src')
    expect(result.output).toContain('package.json')
    expect(result.output).toContain('README.md')
  })

  it('filters out default ignored directories', async () => {
    platform.fs.addFile('/project/src/index.ts', 'x')
    platform.fs.addDir('/project/node_modules')
    platform.fs.addFile('/project/node_modules/foo/index.js', 'x')
    platform.fs.addDir('/project/.git')
    platform.fs.addFile('/project/.git/HEAD', 'ref: refs/heads/main')

    const result = await lsTool.execute({ path: '/project' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).not.toContain('node_modules')
    expect(result.output).not.toContain('.git')
    expect(result.output).toContain('src')
  })

  it('uses working directory when path is not specified', async () => {
    platform.fs.addFile('/project/app.ts', 'x')
    const result = await lsTool.execute({}, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('app.ts')
  })

  it('limits number of files shown', async () => {
    // Add many files
    for (let i = 0; i < 10; i++) {
      platform.fs.addFile(`/project/file-${i}.ts`, 'x')
    }

    const result = await lsTool.execute({ path: '/project', maxFiles: 3 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('and 7 more')
  })

  it('returns error for nonexistent directory', async () => {
    const result = await lsTool.execute({ path: '/nonexistent' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('Failed to list')
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await lsTool.execute(
      { path: '/project' },
      {
        sessionId: 'test',
        workingDirectory: '/project',
        signal: controller.signal,
      }
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })
})
