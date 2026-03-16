/**
 * Tool Execution — Constants and Helpers
 *
 * File path extraction and tool classification constants.
 * The diff capture middleware is no longer used — diffs are captured
 * in the Rust backend. This module is retained for constants and
 * the getModifiedFilePath helper.
 */

import type { SessionBridge } from './types'

// ============================================================================
// Constants
// ============================================================================

/** Tools that modify files and should have diffs captured */
export const DIFF_TOOLS = new Set([
  'write_file',
  'create_file',
  'edit',
  'delete_file',
  'delete',
  'multiedit',
])

/** Max file size to capture for diff (500KB) */
export const MAX_CAPTURE = 500_000

/** Tools excluded in solo mode to save ~2,500 tokens/turn */
export const SOLO_EXCLUDED = new Set([
  // LSP — not wired in desktop yet
  'lsp_diagnostics',
  'lsp_hover',
  'lsp_definition',
  'lsp_references',
  'lsp_document_symbols',
  'lsp_workspace_symbols',
  'lsp_code_actions',
  'lsp_rename',
  'lsp_completions',
  // Delegation/subagent
  'task',
  'sandbox_run',
  // Meta-tools rarely used autonomously
  // Redundant with core tools or rarely needed
  'pty',
  'batch',
  'multiedit',
  'apply_patch',
  // Memory — adds 4 tool definitions
  'memory_read',
  'memory_write',
  'memory_list',
  'memory_delete',
  // Session management
  'plan_enter',
  'plan_exit',
  'recall',
])

// ============================================================================
// Diff Capture Middleware — DEPRECATED
// ============================================================================

/** Tool middleware type for backward compat */
export interface ToolMiddleware {
  name: string
  priority: number
  before?: (ctx: { toolName: string; args: Record<string, unknown> }) => Promise<unknown>
  after?: (ctx: { toolName: string; args: Record<string, unknown> }, result: unknown) => Promise<unknown>
}

/**
 * @deprecated Diff capture now happens in the Rust backend.
 * Returns a no-op middleware for backward compatibility.
 */
export function createDiffCaptureMiddleware(
  _sessionId: string,
  _sessionBridge: SessionBridge
): ToolMiddleware {
  return {
    name: 'chat-diff-capture',
    priority: 25,
  }
}

// ============================================================================
// File Path Extraction
// ============================================================================

/** Extract the file path from a file-modifying tool's input */
export function getModifiedFilePath(
  toolName: string,
  input: Record<string, unknown>
): string | null {
  if (toolName === 'write_file' || toolName === 'create_file') return (input.path as string) || null
  if (toolName === 'edit') return (input.filePath as string) || null
  if (toolName === 'apply_patch') return (input.filePath as string) || null
  if (toolName === 'multiedit') return (input.filePath as string) || null
  if (toolName === 'delete_file' || toolName === 'delete') return (input.path as string) || null
  return null
}
