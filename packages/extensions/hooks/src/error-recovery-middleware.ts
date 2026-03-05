import { dispatchCompute } from '@ava/core-v2'
import type { ExtensionAPI, ToolMiddleware } from '@ava/core-v2/extensions'
import type { ToolResult } from '@ava/core-v2/tools'

const MAX_RETRIES = 3

const FATAL_PATTERNS = [
  'no space left',
  'eacces',
  'permission denied',
  'read-only file system',
  'eisdir',
  'is a directory',
  'enomem',
  'out of memory',
]

function classifyFailureText(text: string): 'fatal' | 'recoverable' {
  const normalized = text.toLowerCase()
  if (FATAL_PATTERNS.some((pattern) => normalized.includes(pattern))) return 'fatal'
  return 'recoverable'
}

function fallbackReplace(content: string, oldString: string, newString: string): string {
  if (!content.includes(oldString)) {
    throw new Error('old string not found')
  }
  return content.replace(oldString, newString)
}

function buildAttemptVariants(oldString: string): string[] {
  const trimmed = oldString
    .split('\n')
    .map((line) => line.trim())
    .join('\n')
  const whitespaceNormalized = oldString.replace(/\s+/g, ' ').trim()
  return [oldString, trimmed, whitespaceNormalized]
}

function withRecoveryMetadata(result: ToolResult, metadata: Record<string, unknown>): ToolResult {
  return {
    ...result,
    metadata: {
      ...result.metadata,
      recovery: metadata,
    },
  }
}

export function createErrorRecoveryMiddleware(
  platform: ExtensionAPI['platform'],
  logger: ExtensionAPI['log']
): ToolMiddleware {
  return {
    name: 'error-recovery',
    priority: 15,
    async after(context, result) {
      if (result.success) {
        return undefined
      }

      const classification = classifyFailureText(result.error ?? result.output)
      if (classification === 'fatal') {
        return {
          result: withRecoveryMetadata(result, {
            classification,
            attempted: 0,
            recovered: false,
          }),
        }
      }

      if (context.toolName !== 'edit') {
        return {
          result: withRecoveryMetadata(result, {
            classification,
            attempted: 0,
            recovered: false,
          }),
        }
      }

      const filePath = typeof context.args.filePath === 'string' ? context.args.filePath : null
      const oldString = typeof context.args.oldString === 'string' ? context.args.oldString : null
      const newString = typeof context.args.newString === 'string' ? context.args.newString : null
      if (!filePath || !oldString || !newString) {
        return {
          result: withRecoveryMetadata(result, {
            classification,
            attempted: 0,
            recovered: false,
          }),
        }
      }

      const variants = buildAttemptVariants(oldString)

      for (let attempt = 1; attempt <= MAX_RETRIES; attempt += 1) {
        const candidateOld = variants[Math.min(attempt - 1, variants.length - 1)] ?? oldString
        try {
          const content = await platform.fs.readFile(filePath)
          const candidateContent = await dispatchCompute<string>(
            'compute_fuzzy_replace',
            {
              content,
              oldString: candidateOld,
              newString,
              replaceAll: false,
            },
            async () => fallbackReplace(content, candidateOld, newString)
          )

          const validation = await dispatchCompute<{ valid: boolean; error?: string }>(
            'validation_validate_edit',
            { content: candidateContent },
            async () => ({ valid: true })
          )

          if (!validation.valid) {
            throw new Error(validation.error ?? 'validation failed')
          }

          await platform.fs.writeFile(filePath, candidateContent)
          const recovered: ToolResult = {
            success: true,
            output: `Recovered edit after ${attempt} retr${attempt === 1 ? 'y' : 'ies'}`,
            metadata: {
              recovered: true,
              retries: attempt,
            },
          }
          return {
            result: withRecoveryMetadata(recovered, {
              classification,
              attempted: attempt,
              recovered: true,
            }),
          }
        } catch (error) {
          logger.warn('Edit recovery retry failed', {
            filePath,
            attempt,
            error: error instanceof Error ? error.message : String(error),
          })
        }
      }

      return {
        result: withRecoveryMetadata(result, {
          classification,
          attempted: MAX_RETRIES,
          recovered: false,
        }),
      }
    },
  }
}
