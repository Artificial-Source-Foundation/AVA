import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { TauriFileSystem } from '../src/fs.js'

/**
 * Tests for TauriFileSystem
 *
 * Note: These tests are limited in a non-Tauri environment since
 * they require the actual Tauri FS plugin to be available.
 */
describe('TauriFileSystem', () => {
  let fs: TauriFileSystem

  beforeEach(() => {
    fs = new TauriFileSystem()
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  describe('normalizePath', () => {
    it('should normalize absolute paths', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('/a/b/c')).toBe('/a/b/c')
    })

    it('should remove redundant slashes', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('/a//b///c')).toBe('/a/b/c')
    })

    it('should resolve . in path', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('/a/./b/./c')).toBe('/a/b/c')
    })

    it('should resolve .. in path', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('/a/b/../c')).toBe('/a/c')
    })

    it('should preserve .. at start of relative path', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('../a/b')).toBe('../a/b')
    })

    it('should normalize multiple ..', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('/a/b/../../c')).toBe('/c')
    })

    it('should handle relative paths', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('a/b/c')).toBe('a/b/c')
    })

    it('should handle root path', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('/')).toBe('/')
    })

    it('should handle empty path components', () => {
      // @ts-expect-error - Testing private method
      expect(fs.normalizePath('/a//b')).toBe('/a/b')
    })
  })

  describe('realpath', () => {
    it('should normalize paths even when stat fails', async () => {
      // In test environment without Tauri, stat will fail
      // but path should still be normalized
      const result = await fs.realpath('/a/../b')
      expect(result).toBe('/b')
    })
  })

  describe('matchesGlob', () => {
    it('should match simple patterns', () => {
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('test.ts', '*.ts')).toBe(true)
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('test.js', '*.ts')).toBe(false)
    })

    it('should match directory patterns', () => {
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('src/test.ts', 'src/*.ts')).toBe(true)
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('lib/test.ts', 'src/*.ts')).toBe(false)
    })

    it('should match ** patterns', () => {
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('src/components/Button.ts', '**/*.ts')).toBe(true)
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('deep/nested/path/file.ts', '**/*.ts')).toBe(true)
    })

    it('should match ? patterns', () => {
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('test.ts', 't?st.ts')).toBe(true)
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('tast.ts', 't?st.ts')).toBe(true)
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('teest.ts', 't?st.ts')).toBe(false)
    })

    it('should match alternation patterns', () => {
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('file.ts', '*.{ts,js}')).toBe(true)
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('file.js', '*.{ts,js}')).toBe(true)
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('file.py', '*.{ts,js}')).toBe(false)
    })

    it('should match character class patterns', () => {
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('file1.ts', 'file[0-9].ts')).toBe(true)
      // @ts-expect-error - Testing private method
      expect(fs.matchesGlob('fileA.ts', 'file[0-9].ts')).toBe(false)
    })
  })

  describe('couldMatchInDir', () => {
    it('should return true for ** patterns', () => {
      // @ts-expect-error - Testing private method
      expect(fs.couldMatchInDir('any', '**/*.ts')).toBe(true)
    })

    it('should return true for matching directory prefix', () => {
      // @ts-expect-error - Testing private method
      expect(fs.couldMatchInDir('src', 'src/components/*.ts')).toBe(true)
    })

    it('should return true for no directory in pattern', () => {
      // @ts-expect-error - Testing private method
      expect(fs.couldMatchInDir('src', '*.ts')).toBe(true)
    })
  })

  describe('escapeRegex', () => {
    it('should escape special regex characters', () => {
      // @ts-expect-error - Testing private method
      expect(fs.escapeRegex('.')).toBe('\\.')
      // @ts-expect-error - Testing private method
      expect(fs.escapeRegex('*')).toBe('\\*')
      // @ts-expect-error - Testing private method
      expect(fs.escapeRegex('+')).toBe('\\+')
      // @ts-expect-error - Testing private method
      expect(fs.escapeRegex('?')).toBe('\\?')
      // @ts-expect-error - Testing private method
      expect(fs.escapeRegex('^')).toBe('\\^')
      // @ts-expect-error - Testing private method
      expect(fs.escapeRegex('$')).toBe('\\$')
    })

    it('should not escape normal characters', () => {
      // @ts-expect-error - Testing private method
      expect(fs.escapeRegex('abc')).toBe('abc')
      // @ts-expect-error - Testing private method
      expect(fs.escapeRegex('123')).toBe('123')
    })
  })
})
