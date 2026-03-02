import { describe, expect, it } from 'vitest'
import { ARITY_MAP, extractCommandPrefix } from './arity.js'
import { parseBashTokens } from './bash-parser.js'

describe('ARITY_MAP', () => {
  it('contains at least 50 commands', () => {
    expect(Object.keys(ARITY_MAP).length).toBeGreaterThanOrEqual(50)
  })

  it('has expected arity for known commands', () => {
    expect(ARITY_MAP.ls).toBe(0)
    expect(ARITY_MAP.cd).toBe(1)
    expect(ARITY_MAP.git).toBe(1)
    expect(ARITY_MAP.grep).toBe(2)
    expect(ARITY_MAP.npm).toBe(1)
    expect(ARITY_MAP.pwd).toBe(0)
  })
})

describe('extractCommandPrefix', () => {
  it('extracts just the command for arity-0 commands', () => {
    const tokens = parseBashTokens('ls -la /tmp')
    expect(extractCommandPrefix(tokens)).toEqual(['ls'])
  })

  it('extracts command + subcommand for arity-1 commands', () => {
    const tokens = parseBashTokens('git status --verbose --porcelain')
    expect(extractCommandPrefix(tokens)).toEqual(['git', 'status'])
  })

  it('strips flags and captures only positional args', () => {
    const tokens = parseBashTokens('npm --verbose install express')
    expect(extractCommandPrefix(tokens)).toEqual(['npm', 'install'])
  })

  it('extracts up to 2 positional args for arity-2 commands', () => {
    const tokens = parseBashTokens('grep -rn "pattern" src/')
    expect(extractCommandPrefix(tokens)).toEqual(['grep', 'pattern', 'src/'])
  })

  it('defaults to arity 1 for unknown commands', () => {
    const tokens = parseBashTokens('my-custom-script build --fast')
    expect(extractCommandPrefix(tokens)).toEqual(['my-custom-script', 'build'])
  })

  it('returns empty array for empty command', () => {
    const tokens = parseBashTokens('')
    expect(extractCommandPrefix(tokens)).toEqual([])
  })

  it('handles arity-0 command with no args', () => {
    const tokens = parseBashTokens('pwd')
    expect(extractCommandPrefix(tokens)).toEqual(['pwd'])
  })

  it('handles piped commands (only first command)', () => {
    const tokens = parseBashTokens('cat file.txt | grep pattern')
    // cat has arity 1, so we get ['cat', 'file.txt']
    expect(extractCommandPrefix(tokens)).toEqual(['cat', 'file.txt'])
  })

  it('handles command with only flags and no positional args', () => {
    const tokens = parseBashTokens('git --version')
    // git arity is 1, but --version is a flag, no positional arg available
    expect(extractCommandPrefix(tokens)).toEqual(['git'])
  })
})
