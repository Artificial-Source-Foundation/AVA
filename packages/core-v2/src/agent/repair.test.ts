/**
 * Tests for repairToolName — fuzzy tool name matching.
 */

import { describe, expect, it } from 'vitest'
import { repairToolName } from './repair.js'

const TOOLS = [
  'read_file',
  'write_file',
  'edit',
  'bash',
  'glob',
  'grep',
  'create_file',
  'delete_file',
  'websearch',
  'webfetch',
  'attempt_completion',
  'memory_read',
  'memory_write',
  'lsp_diagnostics',
]

describe('repairToolName', () => {
  it('returns exact match when name exists', () => {
    expect(repairToolName('bash', TOOLS)).toBe('bash')
  })

  it('returns null when no match is found', () => {
    expect(repairToolName('completely_unknown_tool', TOOLS)).toBeNull()
  })

  it('matches case-insensitively', () => {
    expect(repairToolName('Read_File', TOOLS)).toBe('read_file')
    expect(repairToolName('BASH', TOOLS)).toBe('bash')
    expect(repairToolName('WebSearch', TOOLS)).toBe('websearch')
  })

  it('substitutes hyphens for underscores', () => {
    expect(repairToolName('read-file', TOOLS)).toBe('read_file')
    expect(repairToolName('write-file', TOOLS)).toBe('write_file')
    expect(repairToolName('memory-read', TOOLS)).toBe('memory_read')
  })

  it('substitutes underscores for hyphens when tools use hyphens', () => {
    const hyphenTools = ['my-tool', 'another-tool', 'bash']
    expect(repairToolName('my_tool', hyphenTools)).toBe('my-tool')
  })

  it('matches by prefix when input is a unique prefix', () => {
    expect(repairToolName('attempt_comp', TOOLS)).toBe('attempt_completion')
    expect(repairToolName('lsp_diag', TOOLS)).toBe('lsp_diagnostics')
  })

  it('returns null for ambiguous prefix matches', () => {
    // "memory" is a prefix of both memory_read and memory_write
    expect(repairToolName('memory', TOOLS)).toBeNull()
  })

  it('handles combined case + hyphen repair', () => {
    expect(repairToolName('Read-File', TOOLS)).toBe('read_file')
    expect(repairToolName('Memory-Write', TOOLS)).toBe('memory_write')
  })

  it('returns null for empty input', () => {
    expect(repairToolName('', TOOLS)).toBeNull()
  })

  it('returns null when available tools list is empty', () => {
    expect(repairToolName('bash', [])).toBeNull()
  })
})
