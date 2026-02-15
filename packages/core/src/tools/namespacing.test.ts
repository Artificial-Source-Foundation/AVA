/**
 * Tool Namespacing Tests
 */

import { describe, expect, it } from 'vitest'
import {
  EXT_TOOL_PREFIX,
  extToolName,
  getBareName,
  getNamespace,
  getSource,
  isExtTool,
  isMcpTool,
  isNamespaced,
  lookupTool,
  MCP_TOOL_PREFIX,
  mcpToolName,
  namespaceTool,
  stripNamespace,
} from './namespacing.js'

// ============================================================================
// namespaceTool
// ============================================================================

describe('namespaceTool', () => {
  it('creates mcp-prefixed tool name', () => {
    expect(namespaceTool('mcp', 'github', 'create_issue')).toBe('mcp__github__create_issue')
  })

  it('creates ext-prefixed tool name', () => {
    expect(namespaceTool('ext', 'docker', 'build')).toBe('ext__docker__build')
  })
})

// ============================================================================
// stripNamespace
// ============================================================================

describe('stripNamespace', () => {
  it('strips mcp namespace', () => {
    const result = stripNamespace('mcp__github__create_issue')
    expect(result.prefix).toBe('mcp')
    expect(result.source).toBe('github')
    expect(result.name).toBe('create_issue')
  })

  it('strips ext namespace', () => {
    const result = stripNamespace('ext__docker__build')
    expect(result.prefix).toBe('ext')
    expect(result.source).toBe('docker')
    expect(result.name).toBe('build')
  })

  it('returns bare name for non-namespaced', () => {
    const result = stripNamespace('read_file')
    expect(result.prefix).toBeNull()
    expect(result.source).toBeNull()
    expect(result.name).toBe('read_file')
  })

  it('handles tool names with underscores', () => {
    const result = stripNamespace('mcp__server__tool_with_underscores')
    expect(result.name).toBe('tool_with_underscores')
  })
})

// ============================================================================
// isNamespaced
// ============================================================================

describe('isNamespaced', () => {
  it('returns true for MCP tools', () => {
    expect(isNamespaced('mcp__github__create_issue')).toBe(true)
  })

  it('returns true for ext tools', () => {
    expect(isNamespaced('ext__docker__build')).toBe(true)
  })

  it('returns false for built-in tools', () => {
    expect(isNamespaced('read_file')).toBe(false)
    expect(isNamespaced('bash')).toBe(false)
  })
})

// ============================================================================
// getNamespace
// ============================================================================

describe('getNamespace', () => {
  it('returns mcp for MCP tools', () => {
    expect(getNamespace('mcp__server__tool')).toBe('mcp')
  })

  it('returns ext for extension tools', () => {
    expect(getNamespace('ext__plugin__tool')).toBe('ext')
  })

  it('returns null for built-in tools', () => {
    expect(getNamespace('read_file')).toBeNull()
  })
})

// ============================================================================
// getSource / getBareName
// ============================================================================

describe('getSource', () => {
  it('returns server name for MCP tools', () => {
    expect(getSource('mcp__github__create_issue')).toBe('github')
  })

  it('returns null for non-namespaced', () => {
    expect(getSource('read_file')).toBeNull()
  })
})

describe('getBareName', () => {
  it('returns tool name without namespace', () => {
    expect(getBareName('mcp__github__create_issue')).toBe('create_issue')
  })

  it('returns same name for non-namespaced', () => {
    expect(getBareName('read_file')).toBe('read_file')
  })
})

// ============================================================================
// MCP Helpers
// ============================================================================

describe('mcpToolName', () => {
  it('creates MCP tool name', () => {
    expect(mcpToolName('github', 'create_issue')).toBe('mcp__github__create_issue')
  })

  it('starts with MCP prefix', () => {
    expect(mcpToolName('server', 'tool').startsWith(MCP_TOOL_PREFIX)).toBe(true)
  })
})

describe('isMcpTool', () => {
  it('returns true for MCP tools', () => {
    expect(isMcpTool('mcp__github__create_issue')).toBe(true)
  })

  it('returns false for non-MCP tools', () => {
    expect(isMcpTool('ext__docker__build')).toBe(false)
    expect(isMcpTool('read_file')).toBe(false)
  })
})

// ============================================================================
// Extension Helpers
// ============================================================================

describe('extToolName', () => {
  it('creates ext tool name', () => {
    expect(extToolName('docker', 'build')).toBe('ext__docker__build')
  })

  it('starts with ext prefix', () => {
    expect(extToolName('plugin', 'tool').startsWith(EXT_TOOL_PREFIX)).toBe(true)
  })
})

describe('isExtTool', () => {
  it('returns true for ext tools', () => {
    expect(isExtTool('ext__docker__build')).toBe(true)
  })

  it('returns false for non-ext tools', () => {
    expect(isExtTool('mcp__github__create_issue')).toBe(false)
    expect(isExtTool('read_file')).toBe(false)
  })
})

// ============================================================================
// lookupTool
// ============================================================================

describe('lookupTool', () => {
  it('finds exact match', () => {
    const registry = new Map([['read_file', 'reader']])
    const result = lookupTool('read_file', registry)
    expect(result).not.toBeNull()
    expect(result!.fullName).toBe('read_file')
    expect(result!.value).toBe('reader')
  })

  it('finds namespaced tool by bare name', () => {
    const registry = new Map([['mcp__github__create_issue', 'creator']])
    const result = lookupTool('create_issue', registry)
    expect(result).not.toBeNull()
    expect(result!.fullName).toBe('mcp__github__create_issue')
  })

  it('prefers exact match over fuzzy', () => {
    const registry = new Map([
      ['create_issue', 'builtin'],
      ['mcp__github__create_issue', 'mcp'],
    ])
    const result = lookupTool('create_issue', registry)
    expect(result!.value).toBe('builtin')
  })

  it('returns null for unknown tool', () => {
    const registry = new Map([['read_file', 'reader']])
    expect(lookupTool('unknown', registry)).toBeNull()
  })

  it('returns null for namespaced lookup that does not exist', () => {
    const registry = new Map([['read_file', 'reader']])
    expect(lookupTool('mcp__server__tool', registry)).toBeNull()
  })
})
