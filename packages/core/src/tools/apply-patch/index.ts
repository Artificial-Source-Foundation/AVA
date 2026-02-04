/**
 * Apply Patch Tool
 * Apply unified diff patches to files
 */

import { z } from 'zod'
import { defineTool } from '../define.js'
import { ToolErrorType } from '../errors.js'
import type { ToolResult } from '../types.js'
import { applyPatch, type PatchApplyResult } from './applier.js'
import { parsePatch, validatePatch } from './parser.js'

// Re-export types
export type { FileApplyResult, PatchApplyResult } from './applier.js'
// Re-export functions
export { applyPatch } from './applier.js'
export type { ParsedPatch, PatchChunk, PatchFile, PatchLine, PatchOperation } from './parser.js'
export { parsePatch, validatePatch } from './parser.js'

// ============================================================================
// Schema
// ============================================================================

const ApplyPatchSchema = z.object({
  patch: z.string().describe('The patch content in unified diff format'),
  dryRun: z.boolean().optional().describe('If true, validate without applying changes'),
})

type ApplyPatchParams = z.infer<typeof ApplyPatchSchema>

// ============================================================================
// Tool Implementation
// ============================================================================

export const applyPatchTool = defineTool({
  name: 'apply_patch',
  description: `Apply a unified diff patch to files.

Supports multiple operations:
- Add File: Create a new file
- Update File: Modify existing file with diff
- Delete File: Remove a file
- Move File: Move/rename a file

Patch format:
\`\`\`
*** Begin Patch
*** Add File: path/to/new.txt
+new line 1
+new line 2

*** Update File: path/to/existing.txt
@@ function_name @@
-old line
+new line
 context line

*** Delete File: path/to/remove.txt

*** Move File: old/path.txt -> new/path.txt
*** End Patch
\`\`\`

Features:
- Fuzzy context matching for reliable application
- Atomic operations (validates before applying)
- Dry-run mode for validation without changes

Use this tool when:
- Applying multiple file changes in one operation
- Working with GPT models that prefer diff format
- Need reliable, reversible file modifications`,

  schema: ApplyPatchSchema,

  permissions: ['write', 'delete'],

  async execute(params: ApplyPatchParams, ctx): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Parse the patch
    const parsed = parsePatch(params.patch)

    // Validate the patch
    const validationErrors = validatePatch(parsed)
    if (validationErrors.length > 0) {
      return {
        success: false,
        output: `Patch validation failed:\n${validationErrors.map((e) => `- ${e}`).join('\n')}`,
        error: 'VALIDATION_ERROR',
      }
    }

    if (parsed.files.length === 0) {
      return {
        success: false,
        output: 'Patch contains no file operations',
        error: 'EMPTY_PATCH',
      }
    }

    // Apply the patch
    const result = await applyPatch(parsed, ctx.workingDirectory, params.dryRun === true)

    // Format output
    const output = formatPatchResult(result, params.dryRun === true)

    // Stream metadata if available
    if (ctx.metadata) {
      ctx.metadata({
        title: params.dryRun
          ? `Dry run: ${result.totalOperations} operations`
          : `Applied patch: ${result.successCount}/${result.totalOperations} succeeded`,
        metadata: {
          totalOperations: result.totalOperations,
          successCount: result.successCount,
          failureCount: result.failureCount,
          dryRun: params.dryRun === true,
        },
      })
    }

    return {
      success: result.success,
      output,
      metadata: {
        totalOperations: result.totalOperations,
        successCount: result.successCount,
        failureCount: result.failureCount,
        dryRun: params.dryRun === true,
        files: result.files.map((f) => ({
          path: f.path,
          operation: f.operation,
          success: f.success,
          error: f.error,
        })),
      },
      locations: result.files
        .filter((f) => f.success)
        .map((f) => ({
          path: f.path,
          type: f.operation === 'delete' ? ('delete' as const) : ('write' as const),
        })),
    }
  },
})

// ============================================================================
// Output Formatting
// ============================================================================

function formatPatchResult(result: PatchApplyResult, dryRun: boolean): string {
  const lines: string[] = []
  const mode = dryRun ? 'Dry Run' : 'Applied'

  lines.push(`## Patch ${mode} Results`)
  lines.push(``)
  lines.push(
    `**Summary:** ${result.successCount}/${result.totalOperations} operations ${dryRun ? 'would succeed' : 'succeeded'}`
  )

  if (result.failureCount > 0) {
    lines.push(`**Failures:** ${result.failureCount}`)
  }

  lines.push(``)
  lines.push(`---`)
  lines.push(``)

  for (const file of result.files) {
    const status = file.success ? '✓' : '✗'
    const operation = file.operation.charAt(0).toUpperCase() + file.operation.slice(1)

    lines.push(`### ${status} ${operation}: ${file.path}`)

    if (file.error) {
      lines.push(``)
      lines.push(`**Error:** ${file.error}`)
    }

    lines.push(``)
  }

  if (result.error) {
    lines.push(`---`)
    lines.push(``)
    lines.push(`**Critical Error:** ${result.error}`)
  }

  return lines.join('\n')
}
