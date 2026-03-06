/**
 * Tool Execution — Diff Capture Middleware
 *
 * Captures file diffs for tool calls that modify files.
 * Registers a temporary ToolMiddleware that snapshots file content
 * before and after file-modifying tools execute.
 */

import type { ToolMiddleware, ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { readFileContent } from '../../services/file-browser'
import { recordFileChange } from '../../services/file-versions'
import type { FileOperationType } from '../../types'
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
// Diff Capture Middleware
// ============================================================================

/**
 * Create a ToolMiddleware that captures before/after file content for
 * file-modifying tool calls. Captured diffs are stored as FileOperations.
 */
export function createDiffCaptureMiddleware(
  sessionId: string,
  sessionBridge: SessionBridge
): ToolMiddleware {
  const originalContents = new Map<string, string | null>()

  return {
    name: 'chat-diff-capture',
    priority: 25,

    async before(ctx: ToolMiddlewareContext) {
      const filePath = getModifiedFilePath(ctx.toolName, ctx.args)
      if (filePath && DIFF_TOOLS.has(ctx.toolName)) {
        try {
          const content = await readFileContent(filePath)
          originalContents.set(filePath, content && content.length <= MAX_CAPTURE ? content : null)
        } catch {
          originalContents.set(filePath, null)
        }
      }
      return undefined
    },

    async after(ctx: ToolMiddlewareContext, result) {
      if (!result) return undefined

      const filePath = getModifiedFilePath(ctx.toolName, ctx.args)
      if (!filePath || !result.success || !DIFF_TOOLS.has(ctx.toolName)) return undefined

      const originalContent = originalContents.get(filePath) ?? null
      originalContents.delete(filePath)

      let newContent: string | null = null
      if (ctx.toolName === 'delete_file' || ctx.toolName === 'delete') {
        newContent = null
      } else {
        try {
          const content = await readFileContent(filePath)
          newContent = content && content.length <= MAX_CAPTURE ? content : null
        } catch {
          /* file may not exist after failure */
        }
      }

      const opType: FileOperationType =
        ctx.toolName === 'edit' || ctx.toolName === 'apply_patch' || ctx.toolName === 'multiedit'
          ? 'edit'
          : ctx.toolName === 'create_file'
            ? 'write'
            : ctx.toolName === 'delete_file' || ctx.toolName === 'delete'
              ? 'delete'
              : 'write'

      const oldLines = originalContent?.split('\n').length ?? 0
      const newLines = newContent?.split('\n').length ?? 0

      const fileOp = {
        id: `${sessionId}-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
        sessionId,
        type: opType,
        filePath,
        timestamp: Date.now(),
        originalContent: originalContent ?? undefined,
        newContent: newContent ?? undefined,
        linesAdded: newLines > oldLines ? newLines - oldLines : 0,
        linesRemoved: oldLines > newLines ? oldLines - newLines : 0,
        isNew: originalContent === null && opType === 'write',
      }
      sessionBridge.addFileOperation(fileOp)
      recordFileChange(sessionId, fileOp)

      return undefined
    },
  }
}

// ============================================================================
// File Path Extraction (re-exported from chat/tool-execution)
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
