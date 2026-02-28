import { describe, expect, it } from 'vitest'
import { formatDiagnostics, formatHover, formatLocations } from './queries.js'
import type { LSPDiagnostic, LSPHoverResult, LSPLocation } from './types.js'

describe('formatHover', () => {
  it('formats string contents', () => {
    expect(formatHover({ contents: 'Hello world' })).toBe('Hello world')
  })

  it('formats MarkupContent', () => {
    expect(formatHover({ contents: { kind: 'markdown', value: '**bold**' } })).toBe('**bold**')
  })

  it('formats array contents', () => {
    const hover: LSPHoverResult = {
      contents: ['First line', { language: 'typescript', value: 'const x: number' }],
    }
    const result = formatHover(hover)
    expect(result).toContain('First line')
    expect(result).toContain('const x: number')
  })

  it('handles null hover', () => {
    expect(formatHover(null)).toBe('No hover information available.')
  })
})

describe('formatLocations', () => {
  it('formats locations as file:line:col', () => {
    const locations: LSPLocation[] = [
      {
        uri: 'file:///home/user/test.ts',
        range: { start: { line: 9, character: 4 }, end: { line: 9, character: 10 } },
      },
    ]
    expect(formatLocations(locations)).toBe('/home/user/test.ts:10:5')
  })

  it('handles multiple locations', () => {
    const locations: LSPLocation[] = [
      {
        uri: 'file:///a.ts',
        range: { start: { line: 0, character: 0 }, end: { line: 0, character: 5 } },
      },
      {
        uri: 'file:///b.ts',
        range: { start: { line: 5, character: 2 }, end: { line: 5, character: 8 } },
      },
    ]
    const result = formatLocations(locations)
    expect(result).toContain('/a.ts:1:1')
    expect(result).toContain('/b.ts:6:3')
  })

  it('handles empty locations', () => {
    expect(formatLocations([])).toBe('No results found.')
  })
})

describe('formatDiagnostics', () => {
  it('formats diagnostics by file', () => {
    const diags: LSPDiagnostic[] = [
      {
        file: '/test.ts',
        line: 10,
        column: 5,
        severity: 'error',
        message: 'Type error',
        source: 'ts',
      },
      { file: '/test.ts', line: 20, column: 1, severity: 'warning', message: 'Unused var' },
    ]
    const result = formatDiagnostics(diags)
    expect(result).toContain('/test.ts:')
    expect(result).toContain('10:5 [error] (ts) Type error')
    expect(result).toContain('20:1 [warning] Unused var')
  })

  it('handles empty diagnostics', () => {
    expect(formatDiagnostics([])).toBe('No diagnostics.')
  })

  it('groups by file', () => {
    const diags: LSPDiagnostic[] = [
      { file: '/a.ts', line: 1, column: 1, severity: 'error', message: 'err a' },
      { file: '/b.ts', line: 2, column: 2, severity: 'warning', message: 'warn b' },
    ]
    const result = formatDiagnostics(diags)
    expect(result).toContain('/a.ts:')
    expect(result).toContain('/b.ts:')
  })
})
