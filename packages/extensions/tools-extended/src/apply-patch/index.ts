/**
 * Apply Patch Tool — apply unified diff patches to files.
 *
 * Ported from packages/core/src/tools/apply-patch/index.ts.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import { StreamingDiffApplier } from '../streaming-diff/streaming-diff-applier.js'
import { applyPatch, type PatchApplyResult } from './applier.js'
import { parsePatch, validatePatch } from './parser.js'

export type { FileApplyResult, PatchApplyResult } from './applier.js'
export { applyPatch } from './applier.js'
export type { ParsedPatch, PatchChunk, PatchFile, PatchLine, PatchOperation } from './parser.js'
export { parsePatch, validatePatch } from './parser.js'

const ApplyPatchSchema = z.object({
  patch: z.string().describe('The patch content in unified diff format'),
  dryRun: z.boolean().optional().describe('If true, validate without applying changes'),
  streamChunks: z
    .array(z.string())
    .optional()
    .describe(
      'Optional streamed patch chunks. When provided, patch can be empty and chunks are applied incrementally'
    ),
})

type ApplyPatchParams = z.infer<typeof ApplyPatchSchema>

function formatStreamingResult(result: {
  success: boolean
  appliedCount: number
  pendingCount: number
  errors: string[]
}): string {
  const lines = ['## Streaming Patch Results', '']
  lines.push(`**Applied operations:** ${result.appliedCount}`)
  lines.push(`**Pending operations:** ${result.pendingCount}`)
  if (result.errors.length > 0) {
    lines.push(`**Errors:** ${result.errors.length}`)
    for (const error of result.errors.slice(0, 10)) {
      lines.push(`- ${error}`)
    }
  }
  return lines.join('\n')
}

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
  lines.push(``, `---`, ``)

  for (const file of result.files) {
    const status = file.success ? '✓' : '✗'
    const operation = file.operation.charAt(0).toUpperCase() + file.operation.slice(1)
    lines.push(`### ${status} ${operation}: ${file.path}`)
    if (file.error) {
      lines.push(``, `**Error:** ${file.error}`)
    }
    lines.push(``)
  }

  if (result.error) {
    lines.push(`---`, ``, `**Critical Error:** ${result.error}`)
  }

  return lines.join('\n')
}

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
- Dry-run mode for validation without changes`,

  schema: ApplyPatchSchema,

  permissions: ['write', 'delete'],

  async execute(params: ApplyPatchParams, ctx) {
    if (ctx.signal.aborted) {
      return { success: false, output: 'Operation was cancelled', error: 'EXECUTION_ABORTED' }
    }

    if (params.streamChunks && params.streamChunks.length > 0) {
      const applier = new StreamingDiffApplier(ctx.workingDirectory, params.dryRun === true)
      for (const chunk of params.streamChunks) {
        await applier.pushChunk(chunk)
      }
      const final = await applier.finalize()
      return {
        success: final.success,
        output: formatStreamingResult(final),
        error: final.success ? undefined : 'STREAMING_PATCH_FAILED',
        metadata: {
          streaming: true,
          appliedCount: final.appliedCount,
          pendingCount: final.pendingCount,
          errors: final.errors,
          dryRun: params.dryRun === true,
        },
      }
    }

    const parsed = parsePatch(params.patch)

    const validationErrors = validatePatch(parsed)
    if (validationErrors.length > 0) {
      return {
        success: false,
        output: `Patch validation failed:\n${validationErrors.map((e) => `- ${e}`).join('\n')}`,
        error: 'VALIDATION_ERROR',
      }
    }

    if (parsed.files.length === 0) {
      return { success: false, output: 'Patch contains no file operations', error: 'EMPTY_PATCH' }
    }

    const result = await applyPatch(parsed, ctx.workingDirectory, params.dryRun === true)
    const output = formatPatchResult(result, params.dryRun === true)

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
