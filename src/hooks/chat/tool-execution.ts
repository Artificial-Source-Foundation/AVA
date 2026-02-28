/**
 * Tool Execution Helpers
 * Lint checking and file-path extraction for tool calls.
 */

import { executeTool, type ToolContext } from '@ava/core-v2/tools'

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

// ============================================================================
// Lint Checking
// ============================================================================

/** Run linter on a file and return errors, or null if clean */
export async function checkLintErrors(filePath: string, ctx: ToolContext): Promise<string | null> {
  try {
    const result = await executeTool(
      'bash',
      {
        command: `npx biome check "${filePath}" 2>&1 || npx eslint "${filePath}" 2>&1`,
        timeout: 10000,
      },
      ctx
    )
    if (!result.success && result.output) {
      return result.output.split('\n').slice(0, 50).join('\n')
    }
    return null
  } catch {
    return null
  }
}
