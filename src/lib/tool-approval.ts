/**
 * Shared tool approval logic
 * Used by both useChat and useAgent hooks
 */

import { createSignal } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

/** Approval request for dangerous operations */
export interface ApprovalRequest {
  id: string
  type: 'file' | 'command' | 'browser' | 'mcp'
  toolName: string
  args: Record<string, unknown>
  description: string
  riskLevel: 'low' | 'medium' | 'high' | 'critical'
  resolve: (approved: boolean) => void
}

// ============================================================================
// Auto-approval check (pure function)
// ============================================================================

const READ_ONLY_TOOLS = ['glob', 'grep', 'ls', 'websearch', 'webfetch', 'todoread', 'read_file']
const FILE_WRITE_TOOLS = ['create_file', 'write_file', 'delete_file', 'edit']

export function checkAutoApproval(
  toolName: string,
  _args: Record<string, unknown>,
  isToolAutoApproved: (name: string) => boolean
): { approved: boolean; reason?: string } {
  if (isToolAutoApproved(toolName)) {
    return { approved: true, reason: 'User always-allowed' }
  }

  if (READ_ONLY_TOOLS.includes(toolName)) {
    return { approved: true, reason: 'Read-only tool' }
  }

  if (FILE_WRITE_TOOLS.includes(toolName)) {
    return { approved: false, reason: 'File write operation' }
  }

  if (toolName === 'bash') {
    return { approved: false, reason: 'Shell command' }
  }

  if (toolName === 'browser') {
    return { approved: false, reason: 'Browser automation' }
  }

  if (toolName.startsWith('mcp_')) {
    return { approved: false, reason: 'MCP tool' }
  }

  return { approved: false, reason: 'Unknown tool' }
}

// ============================================================================
// Approval gate factory (creates signals + promise wiring)
// ============================================================================

function inferToolType(toolName: string): ApprovalRequest['type'] {
  if (FILE_WRITE_TOOLS.includes(toolName)) return 'file'
  if (toolName === 'bash') return 'command'
  if (toolName === 'browser') return 'browser'
  if (toolName.startsWith('mcp_')) return 'mcp'
  return 'command'
}

function inferRiskLevel(toolName: string): ApprovalRequest['riskLevel'] {
  if (toolName === 'bash') return 'high'
  if (toolName === 'delete_file') return 'high'
  if (toolName === 'browser') return 'medium'
  if (FILE_WRITE_TOOLS.includes(toolName)) return 'medium'
  if (toolName.startsWith('mcp_')) return 'medium'
  return 'low'
}

export function createApprovalGate() {
  const [pendingApproval, setPendingApproval] = createSignal<ApprovalRequest | null>(null)

  function requestApproval(toolName: string, args: Record<string, unknown>): Promise<boolean> {
    return new Promise<boolean>((resolve) => {
      setPendingApproval({
        id: `${toolName}-${Date.now()}`,
        type: inferToolType(toolName),
        toolName,
        args,
        description: `Execute ${toolName}`,
        riskLevel: inferRiskLevel(toolName),
        resolve,
      })
    })
  }

  function resolveApproval(approved: boolean): void {
    const request = pendingApproval()
    if (request) {
      request.resolve(approved)
      setPendingApproval(null)
    }
  }

  return { pendingApproval, setPendingApproval, requestApproval, resolveApproval }
}
