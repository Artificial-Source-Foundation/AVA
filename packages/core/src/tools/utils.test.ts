/**
 * Tool Utilities Tests
 * Tests for pure utility functions in the tools module
 */

import { describe, expect, it } from 'vitest'
import type { FileSuggestion } from './utils.js'
import {
  formatLineNumber,
  formatSuggestions,
  getInteractiveCommands,
  isBinaryExtension,
  isBinaryOutput,
  isInteractiveCommand,
  LIMITS,
  matchesGlob,
  resolvePath,
  shouldSkipDirectory,
  truncate,
  truncateOutput,
} from './utils.js'

// ============================================================================
// isBinaryExtension
// ============================================================================

describe('isBinaryExtension', () => {
  it('should detect .zip as binary', () => {
    expect(isBinaryExtension('archive.zip')).toBe(true)
  })

  it('should detect .png as binary', () => {
    expect(isBinaryExtension('image.png')).toBe(true)
  })

  it('should detect .pdf as binary', () => {
    expect(isBinaryExtension('document.pdf')).toBe(true)
  })

  it('should detect .exe as binary', () => {
    expect(isBinaryExtension('program.exe')).toBe(true)
  })

  it('should detect .wasm as binary', () => {
    expect(isBinaryExtension('module.wasm')).toBe(true)
  })

  it('should detect .mp3 as binary', () => {
    expect(isBinaryExtension('song.mp3')).toBe(true)
  })

  it('should detect .mp4 as binary', () => {
    expect(isBinaryExtension('video.mp4')).toBe(true)
  })

  it('should detect .ttf as binary', () => {
    expect(isBinaryExtension('font.ttf')).toBe(true)
  })

  it('should detect .sqlite as binary', () => {
    expect(isBinaryExtension('data.sqlite')).toBe(true)
  })

  it('should not flag .ts as binary', () => {
    expect(isBinaryExtension('index.ts')).toBe(false)
  })

  it('should not flag .md as binary', () => {
    expect(isBinaryExtension('README.md')).toBe(false)
  })

  it('should not flag .json as binary', () => {
    expect(isBinaryExtension('package.json')).toBe(false)
  })

  it('should handle uppercase extension via lowercase conversion', () => {
    // lastIndexOf('.') grabs from the dot, then .toLowerCase()
    expect(isBinaryExtension('image.PNG')).toBe(true)
  })

  it('should handle path with multiple dots', () => {
    expect(isBinaryExtension('archive.2024.tar.gz')).toBe(true)
  })

  it('should handle deeply nested path', () => {
    expect(isBinaryExtension('/home/user/project/assets/logo.jpg')).toBe(true)
  })
})

// ============================================================================
// isBinaryOutput
// ============================================================================

describe('isBinaryOutput', () => {
  it('should return false for empty array', () => {
    expect(isBinaryOutput(new Uint8Array([]))).toBe(false)
  })

  it('should return false for all printable bytes', () => {
    const data = new Uint8Array([72, 101, 108, 108, 111]) // "Hello"
    expect(isBinaryOutput(data)).toBe(false)
  })

  it('should return true when null byte is present', () => {
    const data = new Uint8Array([72, 0, 108, 108, 111])
    expect(isBinaryOutput(data)).toBe(true)
  })

  it('should return true when null byte is first', () => {
    const data = new Uint8Array([0, 65, 66, 67])
    expect(isBinaryOutput(data)).toBe(true)
  })

  it('should return true when null byte is last', () => {
    const data = new Uint8Array([65, 66, 67, 0])
    expect(isBinaryOutput(data)).toBe(true)
  })

  it('should return false for high non-null bytes (no null check for ratio)', () => {
    // isBinaryOutput only checks for null bytes, not ratio
    const data = new Uint8Array([128, 200, 255, 180])
    expect(isBinaryOutput(data)).toBe(false)
  })
})

// ============================================================================
// resolvePath
// ============================================================================

describe('resolvePath', () => {
  it('should return absolute Unix path as-is', () => {
    expect(resolvePath('/usr/bin/node', '/home/user')).toBe('/usr/bin/node')
  })

  it('should return Windows absolute path as-is', () => {
    expect(resolvePath('C:\\Users\\file.txt', '/home/user')).toBe('C:\\Users\\file.txt')
  })

  it('should return lowercase drive letter Windows path as-is', () => {
    expect(resolvePath('d:/projects/app', '/home/user')).toBe('d:/projects/app')
  })

  it('should resolve relative path against working directory', () => {
    expect(resolvePath('src/index.ts', '/home/user/project')).toBe(
      '/home/user/project/src/index.ts'
    )
  })

  it('should strip ./ prefix and resolve', () => {
    expect(resolvePath('./src/index.ts', '/home/user/project')).toBe(
      '/home/user/project/src/index.ts'
    )
  })

  it('should handle .. parent references', () => {
    expect(resolvePath('../other/file.ts', '/home/user/project')).toBe('/home/user/other/file.ts')
  })

  it('should handle multiple .. parent references', () => {
    expect(resolvePath('../../file.ts', '/home/user/project')).toBe('/home/file.ts')
  })

  it('should handle mixed . and .. references', () => {
    expect(resolvePath('./../sibling/file.ts', '/home/user/project')).toBe(
      '/home/user/sibling/file.ts'
    )
  })

  it('should handle plain filename', () => {
    expect(resolvePath('file.ts', '/home/user')).toBe('/home/user/file.ts')
  })
})

// ============================================================================
// matchesGlob
// ============================================================================

describe('matchesGlob', () => {
  it('should match *.ts against a .ts file', () => {
    expect(matchesGlob('foo.ts', '*.ts')).toBe(true)
  })

  it('should not match *.ts against a .js file', () => {
    expect(matchesGlob('foo.js', '*.ts')).toBe(false)
  })

  it('should match **/*.ts against deeply nested file', () => {
    expect(matchesGlob('a/b/c/foo.ts', '**/*.ts')).toBe(true)
  })

  it('should match ? for single character', () => {
    expect(matchesGlob('a.ts', '?.ts')).toBe(true)
  })

  it('should not match ? for multiple characters', () => {
    expect(matchesGlob('ab.ts', '?.ts')).toBe(false)
  })

  it('should match {a,b} alternatives', () => {
    expect(matchesGlob('file.ts', 'file.{ts,js}')).toBe(true)
    expect(matchesGlob('file.js', 'file.{ts,js}')).toBe(true)
  })

  it('should not match {a,b} when neither matches', () => {
    expect(matchesGlob('file.py', 'file.{ts,js}')).toBe(false)
  })

  it('should not match * across directory separators', () => {
    expect(matchesGlob('a/b.ts', '*.ts')).toBe(false)
  })

  it('should match ** across directory separators', () => {
    expect(matchesGlob('a/b/c', '**')).toBe(true)
  })

  it('should match exact filename', () => {
    expect(matchesGlob('index.ts', 'index.ts')).toBe(true)
  })

  it('should not match different filename', () => {
    expect(matchesGlob('main.ts', 'index.ts')).toBe(false)
  })

  it('should match pattern with directory prefix', () => {
    expect(matchesGlob('src/index.ts', 'src/*.ts')).toBe(true)
  })
})

// ============================================================================
// shouldSkipDirectory
// ============================================================================

describe('shouldSkipDirectory', () => {
  it('should skip node_modules', () => {
    expect(shouldSkipDirectory('node_modules')).toBe(true)
  })

  it('should skip .git', () => {
    expect(shouldSkipDirectory('.git')).toBe(true)
  })

  it('should skip __pycache__', () => {
    expect(shouldSkipDirectory('__pycache__')).toBe(true)
  })

  it('should skip hidden directories starting with dot', () => {
    expect(shouldSkipDirectory('.hidden')).toBe(true)
  })

  it('should skip dist', () => {
    expect(shouldSkipDirectory('dist')).toBe(true)
  })

  it('should skip build', () => {
    expect(shouldSkipDirectory('build')).toBe(true)
  })

  it('should skip coverage', () => {
    expect(shouldSkipDirectory('coverage')).toBe(true)
  })

  it('should skip target (Rust)', () => {
    expect(shouldSkipDirectory('target')).toBe(true)
  })

  it('should not skip src', () => {
    expect(shouldSkipDirectory('src')).toBe(false)
  })

  it('should not skip lib', () => {
    expect(shouldSkipDirectory('lib')).toBe(false)
  })

  it('should not skip packages', () => {
    expect(shouldSkipDirectory('packages')).toBe(false)
  })
})

// ============================================================================
// truncate
// ============================================================================

describe('truncate', () => {
  it('should return string as-is when within limit', () => {
    expect(truncate('hello', 10)).toBe('hello')
  })

  it('should return string as-is when exactly at limit', () => {
    expect(truncate('hello', 5)).toBe('hello')
  })

  it('should truncate and add ellipsis when over limit', () => {
    expect(truncate('hello world', 8)).toBe('hello...')
  })

  it('should handle very short maxLength', () => {
    expect(truncate('hello world', 4)).toBe('h...')
  })
})

// ============================================================================
// formatLineNumber
// ============================================================================

describe('formatLineNumber', () => {
  it('should pad with zeros to minimum width of 5', () => {
    expect(formatLineNumber(1, 10)).toBe('00001')
  })

  it('should pad to width of totalLines digits when larger than 5', () => {
    expect(formatLineNumber(1, 1000000)).toBe('0000001')
  })

  it('should not pad when lineNum fills the width', () => {
    expect(formatLineNumber(99999, 99999)).toBe('99999')
  })

  it('should handle totalLines with exactly 5 digits', () => {
    expect(formatLineNumber(42, 10000)).toBe('00042')
  })
})

// ============================================================================
// truncateOutput
// ============================================================================

describe('truncateOutput', () => {
  it('should return content unchanged when within both limits', () => {
    const result = truncateOutput('line1\nline2\nline3', 10, 1024)
    expect(result.truncated).toBe(false)
    expect(result.content).toBe('line1\nline2\nline3')
  })

  it('should truncate when exceeding line limit', () => {
    const lines = Array.from({ length: 10 }, (_, i) => `line${i}`)
    const result = truncateOutput(lines.join('\n'), 3, 1024 * 1024)
    expect(result.truncated).toBe(true)
    expect(result.content.split('\n').length).toBeLessThanOrEqual(4) // 3 lines + possible trailing
    expect(result.removedLines).toBeGreaterThan(0)
  })

  it('should truncate when exceeding byte limit', () => {
    const bigLine = 'x'.repeat(1000)
    const output = `${bigLine}\n${bigLine}\n${bigLine}`
    const result = truncateOutput(output, 10000, 500)
    expect(result.truncated).toBe(true)
    expect(result.removedBytes).toBeGreaterThan(0)
  })

  it('should report removed lines and bytes', () => {
    const lines = Array.from({ length: 100 }, (_, i) => `line ${i}`)
    const result = truncateOutput(lines.join('\n'), 5, 1024 * 1024)
    expect(result.truncated).toBe(true)
    expect(result.removedLines).toBe(95)
  })

  it('should use default limits when not provided', () => {
    const result = truncateOutput('short')
    expect(result.truncated).toBe(false)
  })
})

// ============================================================================
// isInteractiveCommand
// ============================================================================

describe('isInteractiveCommand', () => {
  it('should detect vim as interactive', () => {
    expect(isInteractiveCommand('vim file.ts')).toBe(true)
  })

  it('should detect nano as interactive', () => {
    expect(isInteractiveCommand('nano config.yml')).toBe(true)
  })

  it('should detect python REPL as interactive', () => {
    expect(isInteractiveCommand('python')).toBe(true)
  })

  it('should detect node REPL as interactive', () => {
    expect(isInteractiveCommand('node')).toBe(true)
  })

  it('should detect ssh as interactive', () => {
    expect(isInteractiveCommand('ssh user@host')).toBe(true)
  })

  it('should detect psql as interactive', () => {
    expect(isInteractiveCommand('psql mydb')).toBe(true)
  })

  it('should detect top as interactive', () => {
    expect(isInteractiveCommand('top')).toBe(true)
  })

  it('should detect htop as interactive', () => {
    expect(isInteractiveCommand('htop')).toBe(true)
  })

  it('should detect bash as interactive', () => {
    expect(isInteractiveCommand('bash')).toBe(true)
  })

  it('should detect full path like /usr/bin/vim', () => {
    expect(isInteractiveCommand('/usr/bin/vim file.ts')).toBe(true)
  })

  it('should not flag ls as interactive', () => {
    expect(isInteractiveCommand('ls -la')).toBe(false)
  })

  it('should not flag grep as interactive', () => {
    expect(isInteractiveCommand('grep -r pattern .')).toBe(false)
  })

  it('should not flag cat as interactive', () => {
    expect(isInteractiveCommand('cat file.txt')).toBe(false)
  })

  it('should detect -i flag as interactive', () => {
    expect(isInteractiveCommand('some-tool -i input')).toBe(true)
  })

  it('should detect --interactive flag', () => {
    expect(isInteractiveCommand('tool --interactive')).toBe(true)
  })

  it('should detect docker run -it as interactive', () => {
    expect(isInteractiveCommand('docker run -it ubuntu bash')).toBe(true)
  })

  it('should detect docker exec -it as interactive', () => {
    expect(isInteractiveCommand('docker exec -it container bash')).toBe(true)
  })

  it('should detect docker run --interactive as interactive', () => {
    expect(isInteractiveCommand('docker run --interactive ubuntu')).toBe(true)
  })
})

// ============================================================================
// getInteractiveCommands
// ============================================================================

describe('getInteractiveCommands', () => {
  it('should return a ReadonlySet', () => {
    const commands = getInteractiveCommands()
    expect(commands).toBeInstanceOf(Set)
  })

  it('should contain vim', () => {
    expect(getInteractiveCommands().has('vim')).toBe(true)
  })

  it('should contain ssh', () => {
    expect(getInteractiveCommands().has('ssh')).toBe(true)
  })

  it('should not contain ls', () => {
    expect(getInteractiveCommands().has('ls')).toBe(false)
  })
})

// ============================================================================
// formatSuggestions
// ============================================================================

describe('formatSuggestions', () => {
  it('should return empty string for empty array', () => {
    expect(formatSuggestions([])).toBe('')
  })

  it('should format single suggestion', () => {
    const suggestions: FileSuggestion[] = [
      { path: '/home/user/file.ts', similarity: 0.9, reason: 'similar_name' },
    ]
    const result = formatSuggestions(suggestions)
    expect(result).toContain('Did you mean:')
    expect(result).toContain('/home/user/file.ts')
  })

  it('should format multiple suggestions', () => {
    const suggestions: FileSuggestion[] = [
      { path: '/home/user/file.ts', similarity: 0.9, reason: 'similar_name' },
      { path: '/home/user/fille.ts', similarity: 0.8, reason: 'common_typo' },
    ]
    const result = formatSuggestions(suggestions)
    expect(result).toContain('Did you mean:')
    expect(result).toContain('/home/user/file.ts')
    expect(result).toContain('/home/user/fille.ts')
  })
})

// ============================================================================
// LIMITS
// ============================================================================

describe('LIMITS', () => {
  it('should have MAX_RESULTS', () => {
    expect(LIMITS.MAX_RESULTS).toBe(100)
  })

  it('should have MAX_LINES', () => {
    expect(LIMITS.MAX_LINES).toBe(2000)
  })

  it('should have MAX_LINE_LENGTH', () => {
    expect(LIMITS.MAX_LINE_LENGTH).toBe(2000)
  })

  it('should have MAX_BYTES as 50KB', () => {
    expect(LIMITS.MAX_BYTES).toBe(50 * 1024)
  })
})
