/**
 * Tests for saveOverflowOutput — overflow tool output saved to disk.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { _internals, saveOverflowOutput } from './output-files.js'

const { OUTPUT_DIR, SEVEN_DAYS_MS } = _internals

let platform: MockPlatform

beforeEach(() => {
  platform = installMockPlatform()
})

afterEach(() => {
  vi.restoreAllMocks()
})

describe('saveOverflowOutput', () => {
  it('saves content to a .txt file under ~/.ava/tool-output/', async () => {
    const content = 'Hello, this is the full tool output'
    const result = await saveOverflowOutput(content)

    expect(result).not.toBeNull()
    expect(result).toMatch(/\.txt$/)
    expect(result!.startsWith(OUTPUT_DIR)).toBe(true)

    // Verify file was written
    const saved = await platform.fs.readFile(result!)
    expect(saved).toBe(content)
  })

  it('creates the output directory if it does not exist', async () => {
    expect(await platform.fs.exists(OUTPUT_DIR)).toBe(false)

    await saveOverflowOutput('test content')

    expect(await platform.fs.isDirectory(OUTPUT_DIR)).toBe(true)
  })

  it('returns null when fs.writeFile fails', async () => {
    // Make mkdir fail to prevent writeFile from working
    vi.spyOn(platform.fs, 'mkdir').mockRejectedValue(new Error('permission denied'))
    vi.spyOn(platform.fs, 'exists').mockResolvedValue(false)

    const result = await saveOverflowOutput('content')
    expect(result).toBeNull()
  })

  it('generates unique filenames for concurrent calls', async () => {
    const [path1, path2] = await Promise.all([
      saveOverflowOutput('content-1'),
      saveOverflowOutput('content-2'),
    ])

    expect(path1).not.toBeNull()
    expect(path2).not.toBeNull()
    expect(path1).not.toBe(path2)
  })

  it('cleans up files older than 7 days', async () => {
    // Pre-create the output directory and an old file
    await platform.fs.mkdir(OUTPUT_DIR)
    const oldFile = `${OUTPUT_DIR}/old-file.txt`
    await platform.fs.writeFile(oldFile, 'old content')

    // Mock stat to return an old mtime for the existing file
    const originalStat = platform.fs.stat.bind(platform.fs)
    vi.spyOn(platform.fs, 'stat').mockImplementation(async (path: string) => {
      if (path === oldFile) {
        return {
          isFile: true,
          isDirectory: false,
          size: 11,
          mtime: Date.now() - SEVEN_DAYS_MS - 1000,
        }
      }
      return originalStat(path)
    })

    // Save new content — should trigger cleanup
    await saveOverflowOutput('new content')

    // Old file should be removed
    expect(await platform.fs.exists(oldFile)).toBe(false)
  })

  it('does not delete files younger than 7 days', async () => {
    // Pre-create the output directory and a recent file
    await platform.fs.mkdir(OUTPUT_DIR)
    const recentFile = `${OUTPUT_DIR}/recent-file.txt`
    await platform.fs.writeFile(recentFile, 'recent content')

    // Save new content — cleanup should spare recent file
    await saveOverflowOutput('new content')

    // Recent file should still exist (stat returns Date.now() mtime by default)
    expect(await platform.fs.exists(recentFile)).toBe(true)
  })
})
