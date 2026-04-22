/**
 * Tool Approval Bridge
 *
 * Replaces the old MessageBus TOOL_CONFIRMATION_REQUEST pattern.
 * Registers a ToolMiddleware that pauses tool execution until the UI
 * approves or denies the call via a SolidJS signal.
 */

import { createSignal } from 'solid-js'

/** Minimal middleware types (replaces @ava/core-v2/extensions import) */
interface ToolMiddlewareContext {
  toolName: string
  args: Record<string, unknown>
}

interface ToolMiddlewareResult {
  blocked?: boolean
  reason?: string
  result?: { success: boolean; output: string; error?: string }
}

interface ToolMiddleware {
  name: string
  priority: number
  before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined>
}

import { checkAutoApproval } from '../lib/tool-approval'

// ─── Types ──────────────────────────────────────────────────────────────────

export interface PendingApproval {
  id: string
  toolName: string
  args: Record<string, unknown>
  description: string
  type: 'command' | 'file' | 'mcp'
  resolve: (approved: boolean) => void
}

// ─── Reactive State ─────────────────────────────────────────────────────────

const [pendingApproval, setPendingApproval] = createSignal<PendingApproval | null>(null)

export { pendingApproval }

/** Resolve the current pending approval */
export function resolveApproval(approved: boolean): void {
  const current = pendingApproval()
  if (current) {
    current.resolve(approved)
    setPendingApproval(null)
  }
}

// ─── Permission Mode ─────────────────────────────────────────────────────────

let _permissionMode: 'ask' | 'auto-approve' = 'ask'

/** Set the current permission mode (called from settings sync).
 *  Also syncs to the Rust backend's permission context. */
export function setPermissionMode(mode: 'ask' | 'auto-approve'): void {
  _permissionMode = mode

  // Sync to Rust backend — map desktop modes to backend levels
  const backendLevel = mode === 'ask' ? 'standard' : 'autoApprove'
  import('../services/rust-bridge').then(({ rustBackend }) => {
    rustBackend.setPermissionLevel(backendLevel).catch(() => {
      // Silently ignore — bridge may not be ready yet during startup
    })
  })
}

// ─── Auto-Approval Check ────────────────────────────────────────────────────

let _isToolAutoApproved: (name: string) => boolean = () => false

/** Set the auto-approval checker (called from settings init) */
export function setAutoApprovalChecker(fn: (name: string) => boolean): void {
  _isToolAutoApproved = fn
}

// ─── Middleware ──────────────────────────────────────────────────────────────

/**
 * Creates a ToolMiddleware (priority 5) that intercepts tool calls.
 * - Auto-approved tools pass through immediately
 * - Other tools create a PendingApproval signal for the UI
 * - UI resolves → middleware returns (or blocks)
 */
export function createApprovalMiddleware(): ToolMiddleware {
  return {
    name: 'desktop-approval',
    priority: 5,
    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      // Auto-approve mode: allow reads + writes + known tools, only prompt for bash
      if (_permissionMode === 'auto-approve') {
        if (ctx.toolName !== 'bash') return undefined
      }

      // Check auto-approval first
      const autoResult = checkAutoApproval(ctx.toolName, ctx.args, _isToolAutoApproved)
      if (autoResult.approved) return undefined

      // Create a promise that the UI will resolve
      const approved = await new Promise<boolean>((resolve) => {
        const type =
          ctx.toolName === 'bash' ? 'command' : ctx.toolName.startsWith('mcp_') ? 'mcp' : 'file'

        setPendingApproval({
          id: crypto.randomUUID(),
          toolName: ctx.toolName,
          args: ctx.args,
          description: `Execute ${ctx.toolName}`,
          type,
          resolve,
        })
      })

      if (!approved) {
        return { blocked: true, reason: 'User denied tool execution' }
      }

      return undefined
    },
  }
}
