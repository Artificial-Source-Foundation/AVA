import { describe, expect, it } from 'vitest'
import { checkAutoApproval, createApprovalGate } from './tool-approval'

// ============================================================================
// checkAutoApproval
// ============================================================================

describe('checkAutoApproval', () => {
  const noAutoApprove = () => false
  const args = {}

  it('auto-approves read-only tools', () => {
    for (const tool of ['read_file', 'glob', 'grep', 'ls', 'websearch', 'webfetch', 'todoread']) {
      const result = checkAutoApproval(tool, args, noAutoApprove)
      expect(result.approved).toBe(true)
    }
  })

  it('denies file write tools', () => {
    for (const tool of ['create_file', 'write_file', 'delete_file', 'edit']) {
      const result = checkAutoApproval(tool, args, noAutoApprove)
      expect(result.approved).toBe(false)
      expect(result.reason).toBe('File write operation')
    }
  })

  it('denies bash', () => {
    const result = checkAutoApproval('bash', args, noAutoApprove)
    expect(result).toEqual({ approved: false, reason: 'Shell command' })
  })

  it('denies browser', () => {
    const result = checkAutoApproval('browser', args, noAutoApprove)
    expect(result).toEqual({ approved: false, reason: 'Browser automation' })
  })

  it('denies mcp_ prefixed tools', () => {
    const result = checkAutoApproval('mcp_github', args, noAutoApprove)
    expect(result).toEqual({ approved: false, reason: 'MCP tool' })
  })

  it('denies unknown tools', () => {
    const result = checkAutoApproval('some_new_tool', args, noAutoApprove)
    expect(result).toEqual({ approved: false, reason: 'Unknown tool' })
  })

  it('respects user always-allow override', () => {
    const alwaysAllow = () => true
    const result = checkAutoApproval('bash', args, alwaysAllow)
    expect(result).toEqual({ approved: true, reason: 'User always-allowed' })
  })
})

// ============================================================================
// createApprovalGate
// ============================================================================

describe('createApprovalGate', () => {
  it('starts with no pending approval', () => {
    const gate = createApprovalGate()
    expect(gate.pendingApproval()).toBeNull()
  })

  it('requestApproval sets a pending request', () => {
    const gate = createApprovalGate()
    gate.requestApproval('bash', { command: 'rm -rf' })
    const pending = gate.pendingApproval()
    expect(pending).not.toBeNull()
    expect(pending!.toolName).toBe('bash')
    expect(pending!.type).toBe('command')
    expect(pending!.riskLevel).toBe('high')
  })

  it('resolveApproval(true) resolves the promise and clears pending', async () => {
    const gate = createApprovalGate()
    const promise = gate.requestApproval('write_file', { path: '/tmp/a' })
    expect(gate.pendingApproval()).not.toBeNull()
    gate.resolveApproval(true)
    const result = await promise
    expect(result).toBe(true)
    expect(gate.pendingApproval()).toBeNull()
  })

  it('resolveApproval(false) resolves the promise with false', async () => {
    const gate = createApprovalGate()
    const promise = gate.requestApproval('delete_file', { path: '/tmp/a' })
    gate.resolveApproval(false)
    const result = await promise
    expect(result).toBe(false)
    expect(gate.pendingApproval()).toBeNull()
  })

  it('infers correct risk levels', () => {
    const gate = createApprovalGate()

    gate.requestApproval('bash', {})
    expect(gate.pendingApproval()!.riskLevel).toBe('high')
    gate.resolveApproval(false)

    gate.requestApproval('delete_file', {})
    expect(gate.pendingApproval()!.riskLevel).toBe('high')
    gate.resolveApproval(false)

    gate.requestApproval('browser', {})
    expect(gate.pendingApproval()!.riskLevel).toBe('medium')
    gate.resolveApproval(false)

    gate.requestApproval('write_file', {})
    expect(gate.pendingApproval()!.riskLevel).toBe('medium')
    gate.resolveApproval(false)

    gate.requestApproval('mcp_github', {})
    expect(gate.pendingApproval()!.riskLevel).toBe('medium')
    gate.resolveApproval(false)
  })

  it('infers correct tool types', () => {
    const gate = createApprovalGate()

    gate.requestApproval('edit', {})
    expect(gate.pendingApproval()!.type).toBe('file')
    gate.resolveApproval(false)

    gate.requestApproval('bash', {})
    expect(gate.pendingApproval()!.type).toBe('command')
    gate.resolveApproval(false)

    gate.requestApproval('browser', {})
    expect(gate.pendingApproval()!.type).toBe('browser')
    gate.resolveApproval(false)

    gate.requestApproval('mcp_slack', {})
    expect(gate.pendingApproval()!.type).toBe('mcp')
    gate.resolveApproval(false)
  })
})
