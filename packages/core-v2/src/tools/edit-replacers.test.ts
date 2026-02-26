import { describe, expect, it } from 'vitest'
import { DEFAULT_REPLACERS, replace } from './edit-replacers.js'

describe('replace', () => {
  // ─── Simple (exact match) ───────────────────────────────────────────

  describe('simple replacement', () => {
    it('replaces exact match', () => {
      expect(replace('hello world', 'world', 'planet', false)).toBe('hello planet')
    })

    it('replaces unique single-char match', () => {
      expect(replace('hello world', 'hello', 'goodbye', false)).toBe('goodbye world')
    })

    it('replaces with empty string', () => {
      expect(replace('hello world', 'world', '', false)).toBe('hello ')
    })

    it('replaces multiline content', () => {
      const content = 'line1\nline2\nline3'
      const result = replace(content, 'line2', 'replaced', false)
      expect(result).toBe('line1\nreplaced\nline3')
    })
  })

  // ─── Replace All ──────────────────────────────────────────────────────

  describe('replaceAll', () => {
    it('replaces all occurrences', () => {
      expect(replace('aaa', 'a', 'b', true)).toBe('bbb')
    })

    it('replaces all in multiline', () => {
      const content = 'foo bar\nfoo baz\nfoo qux'
      const result = replace(content, 'foo', 'replaced', true)
      expect(result).toBe('replaced bar\nreplaced baz\nreplaced qux')
    })
  })

  // ─── Line-trimmed matching ────────────────────────────────────────────

  describe('line-trimmed matching', () => {
    it('matches ignoring leading/trailing whitespace', () => {
      const content = '  function foo() {\n    return 1\n  }'
      const search = 'function foo() {\n  return 1\n}'
      const result = replace(content, search, 'replaced', false)
      expect(result).toBe('replaced')
    })
  })

  // ─── Whitespace-normalized matching ───────────────────────────────────

  describe('whitespace-normalized matching', () => {
    it('matches across whitespace differences', () => {
      const content = 'const   x   =   1'
      const search = 'const x = 1'
      const result = replace(content, search, 'const y = 2', false)
      expect(result).toBe('const y = 2')
    })
  })

  // ─── Block anchor matching ────────────────────────────────────────────

  describe('block anchor matching', () => {
    it('matches block by first and last line', () => {
      const content = [
        'function foo() {',
        '  const a = 1',
        '  const b = 2',
        '  return a + b',
        '}',
      ].join('\n')

      const search = [
        'function foo() {',
        '  const x = 10',
        '  const y = 20',
        '  return x + y',
        '}',
      ].join('\n')

      const replacement = 'function bar() { return 42 }'
      const result = replace(content, search, replacement, false)
      expect(result).toBe(replacement)
    })
  })

  // ─── Indentation-flexible matching ────────────────────────────────────

  describe('indentation-flexible matching', () => {
    it('matches with different indentation', () => {
      const content = '    if (true) {\n      doSomething()\n    }'
      const search = 'if (true) {\n  doSomething()\n}'
      const result = replace(content, search, 'replaced()', false)
      expect(result).toBe('replaced()')
    })
  })

  // ─── CRLF normalization ───────────────────────────────────────────────

  describe('CRLF normalization', () => {
    it('normalizes CRLF in content', () => {
      expect(replace('hello\r\nworld', 'hello\nworld', 'replaced', false)).toBe('replaced')
    })

    it('normalizes CRLF in search string', () => {
      expect(replace('hello\nworld', 'hello\r\nworld', 'replaced', false)).toBe('replaced')
    })
  })

  // ─── Error cases ──────────────────────────────────────────────────────

  describe('error cases', () => {
    it('throws when oldString not found', () => {
      expect(() => replace('hello', 'nonexistent', 'new', false)).toThrow('oldString not found')
    })

    it('skips non-unique matches for single replace', () => {
      // When simple replacer finds duplicates, it moves to next strategy
      // If none can uniquely match, it should eventually throw or use a fuzzy match
      const content = 'foo\nfoo'
      // Simple match finds 'foo' but it's not unique, so it tries other strategies
      expect(() => replace(content, 'foo', 'bar', false)).toThrow('oldString not found')
    })
  })
})

describe('DEFAULT_REPLACERS', () => {
  it('has 5 replacer strategies', () => {
    expect(DEFAULT_REPLACERS).toHaveLength(5)
  })

  it('all replacers are generator functions', () => {
    for (const replacer of DEFAULT_REPLACERS) {
      expect(typeof replacer).toBe('function')
      // Generator functions return iterators
      const result = replacer('test', 'test')
      expect(typeof result[Symbol.iterator]).toBe('function')
    }
  })
})
