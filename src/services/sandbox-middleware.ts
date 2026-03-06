/**
 * Sandbox Middleware for Core Bridge
 *
 * Intercepts file-modifying tool calls when sandbox mode is enabled.
 * Instead of writing to disk, captures changes for user review.
 */

import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import { getPlatform } from '@ava/core-v2/platform'
import { useSandbox } from '../stores/sandbox'

/** Tools whose file writes should be intercepted in sandbox mode */
const SANDBOX_INTERCEPT_TOOLS = new Set(['write_file', 'create_file', 'edit', 'delete_file'])

/**
 * Creates a ToolMiddleware (priority 3) that intercepts file-modifying tool
 * calls when sandbox mode is enabled. Instead of writing to disk, it captures
 * the changes in the sandbox store for user review.
 */
export function createSandboxMiddleware(): ToolMiddleware {
  return {
    name: 'sandbox-intercept',
    priority: 3, // Before approval (5) — sandbox captures even auto-approved writes

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      const sandbox = useSandbox()
      if (!sandbox.sandboxEnabled()) return undefined
      if (!SANDBOX_INTERCEPT_TOOLS.has(ctx.toolName)) return undefined

      const fs = getPlatform().fs

      if (ctx.toolName === 'write_file' || ctx.toolName === 'create_file') {
        const filePath = ctx.args.path as string
        const newContent = ctx.args.content as string

        // Read original content if file exists
        let originalContent = ''
        let changeType: 'create' | 'modify' = 'create'
        try {
          originalContent = await fs.readFile(filePath)
          changeType = 'modify'
        } catch {
          // File doesn't exist — it's a create
        }

        sandbox.addPendingChange({
          filePath,
          originalContent,
          newContent,
          type: changeType,
        })

        return {
          blocked: true,
          reason: `[Sandbox] ${changeType === 'create' ? 'Created' : 'Modified'} file queued for review: ${filePath}`,
          result: {
            success: true,
            output: `[Sandbox mode] Change to ${filePath} has been captured for review. Use the sandbox review panel to apply or reject.`,
          },
        }
      }

      if (ctx.toolName === 'edit') {
        const filePath = ctx.args.filePath as string
        const oldString = ctx.args.oldString as string
        const newString = ctx.args.newString as string

        // Read current file content
        let originalContent = ''
        try {
          originalContent = await fs.readFile(filePath)
        } catch {
          return {
            blocked: true,
            reason: `[Sandbox] File not found: ${filePath}`,
            result: {
              success: false,
              output: `File not found: ${filePath}`,
              error: `Cannot edit non-existent file: ${filePath}`,
            },
          }
        }

        // Apply the edit to produce new content
        const replaceAll = (ctx.args.replaceAll as boolean) ?? false
        let newContent: string
        if (replaceAll) {
          newContent = originalContent.split(oldString).join(newString)
        } else {
          const idx = originalContent.indexOf(oldString)
          if (idx === -1) {
            return {
              blocked: true,
              reason: `[Sandbox] oldString not found in ${filePath}`,
              result: {
                success: false,
                output: `Could not find the specified text in ${filePath}`,
                error: 'oldString not found in file',
              },
            }
          }
          newContent =
            originalContent.substring(0, idx) +
            newString +
            originalContent.substring(idx + oldString.length)
        }

        sandbox.addPendingChange({
          filePath,
          originalContent,
          newContent,
          type: 'modify',
        })

        return {
          blocked: true,
          reason: `[Sandbox] Edit to ${filePath} queued for review`,
          result: {
            success: true,
            output: `[Sandbox mode] Edit to ${filePath} has been captured for review.`,
          },
        }
      }

      if (ctx.toolName === 'delete_file') {
        const filePath = ctx.args.path as string

        let originalContent = ''
        try {
          originalContent = await fs.readFile(filePath)
        } catch {
          return {
            blocked: true,
            reason: `[Sandbox] File not found: ${filePath}`,
            result: {
              success: false,
              output: `File not found: ${filePath}`,
              error: `Cannot delete non-existent file: ${filePath}`,
            },
          }
        }

        sandbox.addPendingChange({
          filePath,
          originalContent,
          newContent: '',
          type: 'delete',
        })

        return {
          blocked: true,
          reason: `[Sandbox] Deletion of ${filePath} queued for review`,
          result: {
            success: true,
            output: `[Sandbox mode] Deletion of ${filePath} has been captured for review.`,
          },
        }
      }

      return undefined
    },
  }
}
