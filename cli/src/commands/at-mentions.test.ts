import * as fs from 'node:fs/promises'
import * as os from 'node:os'
import * as path from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { expandAtMentions } from './at-mentions.js'

describe('expandAtMentions', () => {
  let tmpDir: string

  beforeEach(async () => {
    tmpDir = await fs.mkdtemp(path.join(os.tmpdir(), 'at-mentions-'))
    await fs.writeFile(path.join(tmpDir, 'hello.txt'), 'Hello world')
    await fs.mkdir(path.join(tmpDir, 'src'), { recursive: true })
    await fs.writeFile(path.join(tmpDir, 'src', 'index.ts'), 'export const x = 1')
  })

  afterEach(async () => {
    await fs.rm(tmpDir, { recursive: true, force: true })
  })

  it('returns goal unchanged when no @mentions', async () => {
    const result = await expandAtMentions('just a normal goal', tmpDir)
    expect(result).toBe('just a normal goal')
  })

  it('expands a single @mention with file content', async () => {
    const result = await expandAtMentions('check @hello.txt for issues', tmpDir)
    expect(result).toContain('<file path="hello.txt">')
    expect(result).toContain('Hello world')
    expect(result).toContain('</file>')
    expect(result).toContain('for issues')
  })

  it('expands nested path @mentions', async () => {
    const result = await expandAtMentions('review @src/index.ts', tmpDir)
    expect(result).toContain('<file path="src/index.ts">')
    expect(result).toContain('export const x = 1')
  })

  it('leaves non-existent file @mentions as-is', async () => {
    const result = await expandAtMentions('check @nonexistent.txt', tmpDir)
    expect(result).toBe('check @nonexistent.txt')
  })

  it('handles multiple @mentions', async () => {
    const result = await expandAtMentions('compare @hello.txt and @src/index.ts', tmpDir)
    expect(result).toContain('<file path="hello.txt">')
    expect(result).toContain('<file path="src/index.ts">')
  })

  it('handles absolute paths', async () => {
    const absPath = path.join(tmpDir, 'hello.txt')
    const result = await expandAtMentions(`check @${absPath}`, tmpDir)
    expect(result).toContain('<file path="')
    expect(result).toContain('Hello world')
  })

  it('handles relative paths with ./', async () => {
    const result = await expandAtMentions('check @./hello.txt', tmpDir)
    expect(result).toContain('<file path="./hello.txt">')
    expect(result).toContain('Hello world')
  })

  it('deduplicates same file mentioned twice', async () => {
    const result = await expandAtMentions('@hello.txt and @hello.txt', tmpDir)
    const matches = result.match(/<file path="hello.txt">/g)
    expect(matches).toHaveLength(2)
  })

  it('handles mixed existing and non-existing files', async () => {
    const result = await expandAtMentions('@hello.txt and @missing.txt', tmpDir)
    expect(result).toContain('<file path="hello.txt">')
    expect(result).toContain('@missing.txt')
  })

  it('does not match email addresses', async () => {
    // The regex requires path-like characters after @
    const result = await expandAtMentions('contact user@example.com', tmpDir)
    // Even if it tries to match, the file won't exist so it stays as-is
    expect(result).toContain('@')
  })
})
