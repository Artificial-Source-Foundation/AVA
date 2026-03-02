import { describe, expect, it } from 'vitest'
import { parseBashTokens } from './bash-parser.js'

describe('parseBashTokens', () => {
  it('parses a simple command with no args', () => {
    const result = parseBashTokens('ls')
    expect(result.command).toBe('ls')
    expect(result.args).toEqual([])
    expect(result.pipes).toEqual([])
    expect(result.redirects).toEqual([])
  })

  it('parses a command with positional args', () => {
    const result = parseBashTokens('git status --verbose --porcelain')
    expect(result.command).toBe('git')
    expect(result.args).toEqual(['status', '--verbose', '--porcelain'])
  })

  it('handles single-quoted strings as one token', () => {
    const result = parseBashTokens("echo 'hello world'")
    expect(result.command).toBe('echo')
    expect(result.args).toEqual(['hello world'])
  })

  it('handles double-quoted strings as one token', () => {
    const result = parseBashTokens('grep "foo bar" file.txt')
    expect(result.command).toBe('grep')
    expect(result.args).toEqual(['foo bar', 'file.txt'])
  })

  it('handles escaped characters in double quotes', () => {
    const result = parseBashTokens('echo "hello \\"world\\""')
    expect(result.command).toBe('echo')
    expect(result.args).toEqual(['hello "world"'])
  })

  it('handles backslash escapes outside quotes', () => {
    const result = parseBashTokens('echo hello\\ world')
    expect(result.command).toBe('echo')
    expect(result.args).toEqual(['hello world'])
  })

  it('parses pipe chains', () => {
    const result = parseBashTokens('cat file.txt | grep pattern | wc -l')
    expect(result.command).toBe('cat')
    expect(result.args).toEqual(['file.txt'])
    expect(result.pipes).toEqual([
      ['grep', 'pattern'],
      ['wc', '-l'],
    ])
  })

  it('extracts redirect operators', () => {
    const result = parseBashTokens('echo hello > output.txt')
    expect(result.command).toBe('echo')
    expect(result.args).toEqual(['hello'])
    expect(result.redirects).toEqual(['>', 'output.txt'])
  })

  it('extracts append redirect', () => {
    const result = parseBashTokens('echo hello >> log.txt')
    expect(result.redirects).toEqual(['>>', 'log.txt'])
  })

  it('handles input redirect', () => {
    const result = parseBashTokens('sort < input.txt')
    expect(result.command).toBe('sort')
    expect(result.redirects).toEqual(['<', 'input.txt'])
  })

  it('handles stderr redirect', () => {
    const result = parseBashTokens('make 2> errors.log')
    expect(result.command).toBe('make')
    expect(result.redirects).toEqual(['2>', 'errors.log'])
  })

  it('returns empty tokens for empty string', () => {
    const result = parseBashTokens('')
    expect(result.command).toBe('')
    expect(result.args).toEqual([])
    expect(result.pipes).toEqual([])
    expect(result.redirects).toEqual([])
  })

  it('handles whitespace-only input', () => {
    const result = parseBashTokens('   ')
    expect(result.command).toBe('')
  })

  it('stops at semicolons (only parses first command)', () => {
    const result = parseBashTokens('cd /tmp; ls -la')
    expect(result.command).toBe('cd')
    expect(result.args).toEqual(['/tmp'])
    // ls -la is after ; so not parsed
    expect(result.pipes).toEqual([])
  })

  it('stops at && (only parses first command)', () => {
    const result = parseBashTokens('npm install && npm test')
    expect(result.command).toBe('npm')
    expect(result.args).toEqual(['install'])
  })

  it('handles mixed pipes and redirects', () => {
    const result = parseBashTokens('grep pattern file.txt | sort | uniq > output.txt 2>&1')
    expect(result.command).toBe('grep')
    expect(result.args).toEqual(['pattern', 'file.txt'])
    expect(result.pipes).toEqual([['sort'], ['uniq']])
    expect(result.redirects).toEqual(['>', 'output.txt', '2>&1'])
  })

  it('handles complex npm command', () => {
    const result = parseBashTokens('npx vitest run packages/core/src/test.ts --reporter verbose')
    expect(result.command).toBe('npx')
    expect(result.args).toEqual([
      'vitest',
      'run',
      'packages/core/src/test.ts',
      '--reporter',
      'verbose',
    ])
  })
})
