import { describe, expect, it } from 'vitest'
import { parseFrontmatter, parseGlobs, parseStringArray } from './frontmatter.js'

describe('parseFrontmatter', () => {
  it('parses key-value pairs', () => {
    const raw = '---\nname: test\ndescription: A test\n---\nBody content'
    const { frontmatter, content } = parseFrontmatter(raw)
    expect(frontmatter.name).toBe('test')
    expect(frontmatter.description).toBe('A test')
    expect(content).toBe('Body content')
  })

  it('parses arrays', () => {
    const raw = '---\nglobs:\n  - "*.tsx"\n  - "*.jsx"\n---\nBody'
    const { frontmatter } = parseFrontmatter(raw)
    expect(frontmatter.globs).toEqual(['*.tsx', '*.jsx'])
  })

  it('strips quotes from values', () => {
    const raw = '---\nname: "quoted-name"\n---\nBody'
    const { frontmatter } = parseFrontmatter(raw)
    expect(frontmatter.name).toBe('quoted-name')
  })

  it('skips comments', () => {
    const raw = '---\n# comment\nname: test\n---\nBody'
    const { frontmatter } = parseFrontmatter(raw)
    expect(frontmatter.name).toBe('test')
  })

  it('returns empty frontmatter for files without delimiters', () => {
    const raw = 'Just plain content'
    const { frontmatter, content } = parseFrontmatter(raw)
    expect(frontmatter).toEqual({})
    expect(content).toBe('Just plain content')
  })

  it('handles empty values as array start', () => {
    const raw = '---\nitems:\n  - first\n  - second\n---\nBody'
    const { frontmatter } = parseFrontmatter(raw)
    expect(frontmatter.items).toEqual(['first', 'second'])
  })
})

describe('parseGlobs', () => {
  it('returns empty array for undefined', () => {
    expect(parseGlobs(undefined)).toEqual([])
  })

  it('wraps string in array', () => {
    expect(parseGlobs('*.ts')).toEqual(['*.ts'])
  })

  it('passes through arrays, filtering empty strings', () => {
    expect(parseGlobs(['*.ts', '', '*.js'])).toEqual(['*.ts', '*.js'])
  })
})

describe('parseStringArray', () => {
  it('returns undefined for undefined input', () => {
    expect(parseStringArray(undefined)).toBeUndefined()
  })

  it('wraps string in array', () => {
    expect(parseStringArray('node')).toEqual(['node'])
  })

  it('returns undefined for empty array', () => {
    expect(parseStringArray([])).toBeUndefined()
  })

  it('returns non-empty arrays', () => {
    expect(parseStringArray(['node', 'express'])).toEqual(['node', 'express'])
  })
})
