import { beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import {
  formatLineNumber,
  isBinaryExtension,
  isBinaryFile,
  isBinaryOutput,
  LIMITS,
  matchesGlob,
  resolvePath,
  resolvePathSafe,
  shouldSkipDirectory,
  truncate,
  truncateOutput,
} from './utils.js'

// ─── LIMITS ─────────────────────────────────────────────────────────────────

describe('LIMITS', () => {
  it('has expected defaults', () => {
    expect(LIMITS.MAX_RESULTS).toBe(100)
    expect(LIMITS.MAX_LINES).toBe(2000)
    expect(LIMITS.MAX_LINE_LENGTH).toBe(2000)
    expect(LIMITS.MAX_BYTES).toBe(50 * 1024)
  })
})

// ─── resolvePath ────────────────────────────────────────────────────────────

describe('resolvePath', () => {
  it('returns absolute path unchanged', () => {
    expect(resolvePath('/home/user/file.ts', '/working')).toBe('/home/user/file.ts')
  })

  it('resolves relative path against cwd', () => {
    const result = resolvePath('src/file.ts', '/home/user/project')
    expect(result).toBe('/home/user/project/src/file.ts')
  })

  it('normalizes path with ..', () => {
    const result = resolvePath('../other/file.ts', '/home/user/project')
    expect(result).toBe('/home/user/other/file.ts')
  })

  it('normalizes path with .', () => {
    const result = resolvePath('./file.ts', '/home/user/project')
    expect(result).toBe('/home/user/project/file.ts')
  })

  it('normalizes double slashes', () => {
    const result = resolvePath('/home//user///file.ts', '/working')
    expect(result).toBe('/home/user/file.ts')
  })
})

// ─── resolvePathSafe ────────────────────────────────────────────────────────

describe('resolvePathSafe', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
  })

  it('resolves path within working directory', async () => {
    platform.fs.addFile('/home/user/project/src/file.ts', 'content')
    platform.fs.addDir('/home/user/project')
    const result = await resolvePathSafe('src/file.ts', '/home/user/project')
    expect(result).toBe('/home/user/project/src/file.ts')
  })

  it('returns non-realpath result for non-existent file (creation case)', async () => {
    platform.fs.addDir('/home/user/project')
    const result = await resolvePathSafe('new-file.ts', '/home/user/project')
    expect(result).toBe('/home/user/project/new-file.ts')
  })
})

// ─── Binary Detection ───────────────────────────────────────────────────────

describe('isBinaryExtension', () => {
  it('detects common binary extensions', () => {
    expect(isBinaryExtension('image.png')).toBe(true)
    expect(isBinaryExtension('image.jpg')).toBe(true)
    expect(isBinaryExtension('file.pdf')).toBe(true)
    expect(isBinaryExtension('archive.zip')).toBe(true)
    expect(isBinaryExtension('binary.exe')).toBe(true)
    expect(isBinaryExtension('lib.wasm')).toBe(true)
    expect(isBinaryExtension('font.ttf')).toBe(true)
    expect(isBinaryExtension('data.sqlite')).toBe(true)
  })

  it('returns false for text extensions', () => {
    expect(isBinaryExtension('code.ts')).toBe(false)
    expect(isBinaryExtension('readme.md')).toBe(false)
    expect(isBinaryExtension('config.json')).toBe(false)
    expect(isBinaryExtension('style.css')).toBe(false)
    expect(isBinaryExtension('page.html')).toBe(false)
  })

  it('is case insensitive', () => {
    expect(isBinaryExtension('image.PNG')).toBe(true)
    expect(isBinaryExtension('image.Jpg')).toBe(true)
  })

  it('handles no extension', () => {
    expect(isBinaryExtension('Makefile')).toBe(false)
  })
})

describe('isBinaryFile', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
  })

  it('detects binary by extension', async () => {
    expect(await isBinaryFile('/test.png')).toBe(true)
  })

  it('detects text files', async () => {
    platform.fs.addFile('/test.ts', 'const x = 1')
    expect(await isBinaryFile('/test.ts')).toBe(false)
  })

  it('detects null bytes as binary', async () => {
    platform.fs.addBinary('/test.bin', new Uint8Array([72, 101, 0, 108, 108, 111]))
    expect(await isBinaryFile('/test.bin')).toBe(true)
  })

  it('detects high non-printable ratio as binary', async () => {
    const data = new Uint8Array(100).fill(200) // all non-printable
    platform.fs.addBinary('/test.dat', data)
    expect(await isBinaryFile('/test.dat')).toBe(true)
  })

  it('returns false for empty files', async () => {
    platform.fs.addFile('/empty', '')
    expect(await isBinaryFile('/empty')).toBe(false)
  })

  it('returns false on read error', async () => {
    expect(await isBinaryFile('/nonexistent')).toBe(false)
  })
})

describe('isBinaryOutput', () => {
  it('detects null bytes', () => {
    expect(isBinaryOutput(new Uint8Array([65, 0, 66]))).toBe(true)
  })

  it('returns false for clean text', () => {
    expect(isBinaryOutput(new Uint8Array([65, 66, 67]))).toBe(false)
  })

  it('returns false for empty', () => {
    expect(isBinaryOutput(new Uint8Array([]))).toBe(false)
  })
})

// ─── Output Formatting ──────────────────────────────────────────────────────

describe('truncate', () => {
  it('returns short string unchanged', () => {
    expect(truncate('hello', 10)).toBe('hello')
  })

  it('truncates long string with ellipsis', () => {
    expect(truncate('hello world!', 8)).toBe('hello...')
  })

  it('returns exact length string unchanged', () => {
    expect(truncate('hello', 5)).toBe('hello')
  })
})

describe('formatLineNumber', () => {
  it('pads to 5 by default for small files', () => {
    expect(formatLineNumber(1, 10)).toBe('00001')
  })

  it('pads based on total lines (min width 5)', () => {
    expect(formatLineNumber(42, 1000)).toBe('00042')
  })

  it('handles large line numbers', () => {
    expect(formatLineNumber(12345, 99999)).toBe('12345')
  })
})

describe('truncateOutput', () => {
  it('returns content unchanged if within limits', () => {
    const result = truncateOutput('line1\nline2\nline3')
    expect(result.content).toBe('line1\nline2\nline3')
    expect(result.truncated).toBe(false)
    expect(result.removedLines).toBe(0)
  })

  it('truncates by line count', () => {
    const lines = Array.from({ length: 10 }, (_, i) => `line ${i}`)
    const result = truncateOutput(lines.join('\n'), 5)
    expect(result.truncated).toBe(true)
    expect(result.removedLines).toBe(5)
    expect(result.content.split('\n')).toHaveLength(5)
  })

  it('truncates by byte count', () => {
    const bigLine = 'x'.repeat(100)
    const lines = Array.from({ length: 10 }, () => bigLine)
    const result = truncateOutput(lines.join('\n'), 1000, 250)
    expect(result.truncated).toBe(true)
  })

  it('uses default limits', () => {
    const result = truncateOutput('small')
    expect(result.truncated).toBe(false)
  })
})

// ─── Pattern Matching ───────────────────────────────────────────────────────

describe('shouldSkipDirectory', () => {
  it('skips node_modules', () => {
    expect(shouldSkipDirectory('node_modules')).toBe(true)
  })

  it('skips .git', () => {
    expect(shouldSkipDirectory('.git')).toBe(true)
  })

  it('skips hidden directories', () => {
    expect(shouldSkipDirectory('.hidden')).toBe(true)
    expect(shouldSkipDirectory('.vscode')).toBe(true)
  })

  it('skips known build dirs', () => {
    expect(shouldSkipDirectory('build')).toBe(true)
    expect(shouldSkipDirectory('dist')).toBe(true)
    expect(shouldSkipDirectory('coverage')).toBe(true)
    expect(shouldSkipDirectory('target')).toBe(true)
  })

  it('skips __pycache__', () => {
    expect(shouldSkipDirectory('__pycache__')).toBe(true)
  })

  it('skips venv dirs', () => {
    expect(shouldSkipDirectory('venv')).toBe(true)
    expect(shouldSkipDirectory('.venv')).toBe(true)
  })

  it('does not skip regular directories', () => {
    expect(shouldSkipDirectory('src')).toBe(false)
    expect(shouldSkipDirectory('lib')).toBe(false)
    expect(shouldSkipDirectory('packages')).toBe(false)
  })
})

describe('matchesGlob', () => {
  it('matches simple extension pattern', () => {
    expect(matchesGlob('file.ts', '*.ts')).toBe(true)
    expect(matchesGlob('file.js', '*.ts')).toBe(false)
  })

  it('matches double-star glob', () => {
    expect(matchesGlob('src/components/Button.tsx', '**/*.tsx')).toBe(true)
  })

  it('matches path prefix', () => {
    expect(matchesGlob('src/index.ts', 'src/*.ts')).toBe(true)
    expect(matchesGlob('lib/index.ts', 'src/*.ts')).toBe(false)
  })

  it('handles alternatives with braces', () => {
    expect(matchesGlob('file.ts', '*.{ts,tsx}')).toBe(true)
    expect(matchesGlob('file.tsx', '*.{ts,tsx}')).toBe(true)
    expect(matchesGlob('file.js', '*.{ts,tsx}')).toBe(false)
  })

  it('handles question mark wildcard', () => {
    expect(matchesGlob('file.ts', 'file.t?')).toBe(true)
    expect(matchesGlob('file.tsx', 'file.t?')).toBe(false)
  })

  it('handles exact match', () => {
    expect(matchesGlob('README.md', 'README.md')).toBe(true)
    expect(matchesGlob('CHANGELOG.md', 'README.md')).toBe(false)
  })

  it('handles deep path patterns', () => {
    expect(matchesGlob('src/a/b/c.ts', 'src/**/*.ts')).toBe(true)
  })
})
