/**
 * Policy Matcher Tests
 */

import { describe, expect, it } from 'vitest'
import {
  checkCompoundCommand,
  extractCommandName,
  matchArgs,
  matchToolName,
  stableStringify,
} from './matcher.js'

describe('stableStringify', () => {
  it('should sort object keys', () => {
    const result = stableStringify({ b: 2, a: 1 })
    expect(result).toBe('{"a":1,"b":2}')
  })

  it('should handle nested objects', () => {
    const result = stableStringify({ b: { d: 4, c: 3 }, a: 1 })
    expect(result).toBe('{"a":1,"b":{"c":3,"d":4}}')
  })

  it('should handle arrays', () => {
    const result = stableStringify([3, 1, 2])
    expect(result).toBe('[3,1,2]')
  })

  it('should handle null and undefined', () => {
    expect(stableStringify(null)).toBe('null')
    expect(stableStringify(undefined)).toBe('undefined')
  })

  it('should handle strings with special chars', () => {
    const result = stableStringify({ path: '/foo/bar "baz"' })
    expect(result).toContain('/foo/bar')
  })

  it('should handle circular references', () => {
    const obj: Record<string, unknown> = { a: 1 }
    obj.self = obj
    expect(() => stableStringify(obj)).not.toThrow()
    expect(stableStringify(obj)).toContain('[Circular]')
  })

  it('should handle functions', () => {
    const result = stableStringify({ fn: () => {} })
    expect(result).toContain('[Function]')
  })
})

describe('matchToolName', () => {
  it('should match exact names', () => {
    expect(matchToolName('bash', 'bash')).toBe(true)
    expect(matchToolName('bash', 'read_file')).toBe(false)
  })

  it('should match universal wildcard', () => {
    expect(matchToolName('*', 'anything')).toBe(true)
    expect(matchToolName('*', 'bash')).toBe(true)
  })

  it('should match prefix wildcards', () => {
    expect(matchToolName('mcp__*', 'mcp__github__search')).toBe(true)
    expect(matchToolName('mcp__*', 'mcp__slack__post')).toBe(true)
    expect(matchToolName('mcp__*', 'bash')).toBe(false)
  })

  it('should match delegate wildcards', () => {
    expect(matchToolName('delegate_*', 'delegate_coder')).toBe(true)
    expect(matchToolName('delegate_*', 'delegate_tester')).toBe(true)
    expect(matchToolName('delegate_*', 'read_file')).toBe(false)
  })

  it('should handle MCP server boundary security', () => {
    // mcp__github__* should match mcp__github__search
    expect(matchToolName('mcp__github__*', 'mcp__github__search')).toBe(true)
    // mcp__github__* should NOT match mcp__github_malicious__tool
    expect(matchToolName('mcp__github__*', 'mcp__github_malicious__tool')).toBe(false)
  })
})

describe('matchArgs', () => {
  it('should match regex against stable JSON', () => {
    expect(matchArgs(/npm\s+test/, { command: 'npm test' })).toBe(true)
    expect(matchArgs(/npm\s+test/, { command: 'ls -la' })).toBe(false)
  })

  it('should match path patterns', () => {
    expect(matchArgs(/\.ssh\/id_/, { path: '/home/user/.ssh/id_rsa' })).toBe(true)
    expect(matchArgs(/\.ssh\/id_/, { path: '/home/user/code/app.ts' })).toBe(false)
  })

  it('should match nested args', () => {
    expect(matchArgs(/"action":"delete"/, { action: 'delete', target: 'foo' })).toBe(true)
  })
})

describe('checkCompoundCommand', () => {
  it('should check simple commands', () => {
    const result = checkCompoundCommand('ls -la', () => 'allow')
    expect(result).toBe('allow')
  })

  it('should aggregate piped commands', () => {
    const result = checkCompoundCommand('cat file | grep pattern', (cmd) => {
      if (cmd.trim().startsWith('cat')) return 'allow'
      if (cmd.trim().startsWith('grep')) return 'allow'
      return 'deny'
    })
    expect(result).toBe('allow')
  })

  it('should return deny if any segment is denied', () => {
    const result = checkCompoundCommand('ls && rm -rf /', (cmd) => {
      if (cmd.trim().startsWith('rm')) return 'deny'
      return 'allow'
    })
    expect(result).toBe('deny')
  })

  it('should return ask_user if any segment needs approval', () => {
    const result = checkCompoundCommand('ls && npm install', (cmd) => {
      if (cmd.trim().startsWith('npm')) return 'ask_user'
      return 'allow'
    })
    expect(result).toBe('ask_user')
  })

  it('should handle semicolon separators', () => {
    const result = checkCompoundCommand('echo hi; rm file', (cmd) => {
      if (cmd.trim().startsWith('rm')) return 'deny'
      return 'allow'
    })
    expect(result).toBe('deny')
  })

  it('should downgrade ALLOW to ASK_USER for redirections', () => {
    const result = checkCompoundCommand('echo hello > file.txt', () => 'allow')
    expect(result).toBe('ask_user')
  })

  it('should handle OR operators', () => {
    const result = checkCompoundCommand('test -f file || echo "missing"', () => 'allow')
    expect(result).toBe('allow')
  })
})

describe('extractCommandName', () => {
  it('should extract simple command names', () => {
    expect(extractCommandName('ls -la')).toBe('ls')
    expect(extractCommandName('npm test')).toBe('npm')
    expect(extractCommandName('git status')).toBe('git')
  })

  it('should skip environment variables', () => {
    expect(extractCommandName('NODE_ENV=production node app.js')).toBe('node')
    expect(extractCommandName('FOO=bar BAZ=qux command arg')).toBe('command')
  })

  it('should handle single-word commands', () => {
    expect(extractCommandName('ls')).toBe('ls')
  })
})
