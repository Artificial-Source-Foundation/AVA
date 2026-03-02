/**
 * Tests for token-efficient tool result transformers.
 */

import { describe, expect, it } from 'vitest'
import {
  efficientToolResult,
  groupGrepResults,
  normalizeWhitespace,
  smartSummarize,
  stripAnsi,
  summarizeLsOutput,
} from './efficient-results.js'

// ─── stripAnsi ──────────────────────────────────────────────────────────────

describe('stripAnsi', () => {
  it('removes color codes', () => {
    expect(stripAnsi('\x1B[31mred\x1B[0m')).toBe('red')
  })

  it('removes bold and underline codes', () => {
    expect(stripAnsi('\x1B[1mbold\x1B[22m \x1B[4munderline\x1B[24m')).toBe('bold underline')
  })

  it('passes through plain text unchanged', () => {
    expect(stripAnsi('hello world')).toBe('hello world')
  })

  it('handles empty string', () => {
    expect(stripAnsi('')).toBe('')
  })

  it('removes cursor movement codes', () => {
    expect(stripAnsi('\x1B[2Jhello\x1B[H')).toBe('hello')
  })
})

// ─── normalizeWhitespace ─────────────────────────────────────────────────────

describe('normalizeWhitespace', () => {
  it('collapses multiple spaces to one', () => {
    expect(normalizeWhitespace('a   b   c')).toBe('a b c')
  })

  it('collapses tabs to single space', () => {
    expect(normalizeWhitespace('a\t\tb')).toBe('a b')
  })

  it('trims trailing whitespace per line', () => {
    expect(normalizeWhitespace('hello   \nworld   ')).toBe('hello\nworld')
  })

  it('collapses 3+ blank lines to 2', () => {
    const input = 'a\n\n\n\n\nb'
    const result = normalizeWhitespace(input)
    expect(result).toBe('a\n\n\nb')
  })

  it('trims leading and trailing blank lines', () => {
    expect(normalizeWhitespace('\n\nhello\n\n')).toBe('hello')
  })

  it('handles empty string', () => {
    expect(normalizeWhitespace('')).toBe('')
  })

  it('handles single line', () => {
    expect(normalizeWhitespace('  hello  ')).toBe('hello')
  })
})

// ─── smartSummarize ──────────────────────────────────────────────────────────

describe('smartSummarize', () => {
  it('returns short output unchanged', () => {
    const short = 'hello world'
    expect(smartSummarize(short)).toBe(short)
  })

  it('returns output under threshold unchanged', () => {
    const output = 'line\n'.repeat(10)
    expect(smartSummarize(output, 1000)).toBe(output)
  })

  it('summarizes output over threshold with >70 lines', () => {
    const lines = Array.from({ length: 100 }, (_, i) => `line ${i + 1}`)
    const output = lines.join('\n')
    const result = smartSummarize(output, 100) // low threshold to force summarization

    // Should contain first 50 lines
    expect(result).toContain('line 1')
    expect(result).toContain('line 50')

    // Should contain last 20 lines
    expect(result).toContain('line 81')
    expect(result).toContain('line 100')

    // Should contain omission notice
    expect(result).toContain('30 lines omitted out of 100 total')

    // Should NOT contain middle lines
    expect(result).not.toContain('line 51\n')
    expect(result).not.toContain('line 80\n')
  })

  it('returns output with <=70 lines unchanged even if over byte threshold', () => {
    const lines = Array.from({ length: 60 }, () => 'x'.repeat(200))
    const output = lines.join('\n')
    expect(smartSummarize(output, 100)).toBe(output)
  })

  it('handles empty string', () => {
    expect(smartSummarize('')).toBe('')
  })
})

// ─── groupGrepResults ────────────────────────────────────────────────────────

describe('groupGrepResults', () => {
  it('groups results by file path', () => {
    const output = [
      '/src/app.ts:1:import foo',
      '/src/app.ts:5:export foo',
      '/src/util.ts:10:const bar',
    ].join('\n')

    const result = groupGrepResults(output)
    expect(result).toContain('2 files')
    expect(result).toContain('3 total matches')
    expect(result).toContain('/src/app.ts (2 matches)')
    expect(result).toContain('/src/util.ts (1 match)')
  })

  it('limits shown matches per file to 5', () => {
    const lines = Array.from({ length: 10 }, (_, i) => `/src/big.ts:${i + 1}:line ${i + 1}`)
    const output = lines.join('\n')

    const result = groupGrepResults(output)
    expect(result).toContain('/src/big.ts (10 matches)')
    expect(result).toContain('and 5 more')
  })

  it('returns non-grep output unchanged', () => {
    const output = 'just some text\nno file paths here'
    expect(groupGrepResults(output)).toBe(output)
  })

  it('handles empty input', () => {
    expect(groupGrepResults('')).toBe('')
  })

  it('handles single match', () => {
    const output = '/src/index.ts:42:hello'
    const result = groupGrepResults(output)
    expect(result).toContain('1 files')
    expect(result).toContain('1 total matches')
    expect(result).toContain('/src/index.ts (1 match)')
  })
})

// ─── summarizeLsOutput ──────────────────────────────────────────────────────

describe('summarizeLsOutput', () => {
  it('returns short listings unchanged', () => {
    const output = 'file1.ts\nfile2.ts\nfile3.ts'
    expect(summarizeLsOutput(output)).toBe(output)
  })

  it('truncates listings over 50 entries', () => {
    const files = Array.from({ length: 80 }, (_, i) => `file${i + 1}.ts`)
    const output = files.join('\n')

    const result = summarizeLsOutput(output)
    expect(result).toContain('file1.ts')
    expect(result).toContain('file50.ts')
    expect(result).toContain('30 more entries')
    expect(result).toContain('80 total')
    expect(result).not.toContain('file51.ts')
  })

  it('handles empty input', () => {
    expect(summarizeLsOutput('')).toBe('')
  })

  it('ignores blank lines in count', () => {
    const output = 'a\n\nb\n\nc'
    expect(summarizeLsOutput(output)).toBe(output)
  })
})

// ─── efficientToolResult ─────────────────────────────────────────────────────

describe('efficientToolResult', () => {
  it('returns empty string unchanged', () => {
    expect(efficientToolResult('bash', '')).toBe('')
  })

  it('applies stripAnsi + normalize + summarize for bash', () => {
    const output = '\x1B[31mred text\x1B[0m   with   spaces'
    const result = efficientToolResult('bash', output)
    expect(result).toBe('red text with spaces')
    expect(result).not.toContain('\x1B')
  })

  it('applies groupGrepResults for grep tool', () => {
    const output = ['/src/a.ts:1:match1', '/src/a.ts:2:match2', '/src/b.ts:3:match3'].join('\n')

    const result = efficientToolResult('grep', output)
    expect(result).toContain('2 files')
    expect(result).toContain('3 total matches')
  })

  it('applies summarizeLsOutput for ls tool', () => {
    const files = Array.from({ length: 80 }, (_, i) => `file${i + 1}.ts`)
    const result = efficientToolResult('ls', files.join('\n'))
    expect(result).toContain('30 more entries')
  })

  it('applies summarizeLsOutput for glob tool', () => {
    const files = Array.from({ length: 80 }, (_, i) => `file${i + 1}.ts`)
    const result = efficientToolResult('glob', files.join('\n'))
    expect(result).toContain('30 more entries')
  })

  it('applies default normalization for unknown tools', () => {
    const output = '  extra   spaces  '
    const result = efficientToolResult('unknown_tool', output)
    expect(result).toBe('extra spaces')
  })

  it('handles ripgrep tool name', () => {
    const output = '/src/x.ts:1:found'
    const result = efficientToolResult('ripgrep', output)
    expect(result).toContain('/src/x.ts')
  })
})
