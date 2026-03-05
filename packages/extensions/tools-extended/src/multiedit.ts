/**
 * multiedit tool — apply multiple edits across one or more files.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import * as z from 'zod'
import { runEditCascade } from './edit/cascade.js'
import {
  executeMultiEditJobs,
  type FileEdit,
  type MultiEditExecutionResult,
  type MultiEditJob,
  type MultiEditJobResult,
} from './multiedit-executor.js'

const EditSchema = z.object({
  oldString: z.string().describe('Text to find'),
  newString: z.string().describe('Text to replace with'),
})

const SingleFileSchema = z.object({
  filePath: z.string().describe('Path to the file (absolute or relative to working directory)'),
  edits: z.array(EditSchema).max(50),
  concurrency: z.number().int().min(1).max(16).optional(),
})

const MultiFileSchema = z.object({
  files: z
    .array(
      z.object({
        filePath: z.string().describe('Path to file (absolute or relative to working directory)'),
        edits: z.array(EditSchema).max(50),
      })
    )
    .min(1)
    .max(64),
  concurrency: z.number().int().min(1).max(16).optional(),
})

const MultiEditSchema = z.union([SingleFileSchema, MultiFileSchema])

type SingleFileInput = z.infer<typeof SingleFileSchema>
type MultiEditInput = z.infer<typeof MultiEditSchema>

function isSingle(input: MultiEditInput): input is SingleFileInput {
  return 'filePath' in input
}

function normalizeJobs(input: MultiEditInput): MultiEditJob[] {
  if (isSingle(input)) {
    return [{ filePath: input.filePath, edits: input.edits }]
  }
  return input.files.map((f) => ({ filePath: f.filePath, edits: f.edits }))
}

function getConcurrency(input: MultiEditInput): number | undefined {
  return input.concurrency
}

function formatOutput(result: MultiEditExecutionResult): string {
  if (result.results.length === 1) {
    const item = result.results[0]
    if (item?.success) {
      return `Applied ${item.appliedEdits} edit(s) to ${item.filePath}`
    }
    return `Failed to apply edits to ${item?.filePath ?? 'unknown file'}: ${item?.error ?? 'unknown error'}`
  }

  const lines = [
    `Processed ${result.results.length} file(s): ${result.succeeded} succeeded, ${result.failed} failed`,
  ]
  for (const item of result.results) {
    lines.push(
      item.success
        ? `- OK ${item.filePath}: ${item.appliedEdits} edit(s)`
        : `- FAIL ${item.filePath}: ${item.error ?? 'unknown error'}`
    )
  }
  return lines.join('\n')
}

function primaryError(result: MultiEditExecutionResult): string {
  const firstFailure = result.results.find((r) => !r.success)
  return firstFailure?.error ?? `${result.failed} file edit(s) failed`
}

export const multieditTool = defineTool({
  name: 'multiedit',
  description: 'Apply multiple text replacements across one or more files.',
  schema: MultiEditSchema,
  permissions: ['write'],
  locations: (input) =>
    isSingle(input)
      ? [{ path: input.filePath, type: 'write' }]
      : input.files.map((f) => ({ path: f.filePath, type: 'write' })),
  async execute(input, ctx) {
    if (ctx.signal?.aborted) return { success: false, output: '', error: 'Aborted' }

    const fs = getPlatform().fs
    const jobs = normalizeJobs(input)

    const applyJob = async (job: MultiEditJob): Promise<MultiEditJobResult> => {
      const filePath = await resolvePathSafe(job.filePath, ctx.workingDirectory)
      let content: string
      try {
        content = await fs.readFile(filePath)
      } catch {
        return { filePath, success: false, appliedEdits: 0, error: `File not found: ${filePath}` }
      }

      let modified = content
      for (let i = 0; i < job.edits.length; i++) {
        const edit = job.edits[i] as FileEdit
        try {
          const result = await runEditCascade({
            content: modified,
            oldText: edit.oldString,
            newText: edit.newString,
            maxCorrections: 0,
          })
          modified = result.content
        } catch {
          return {
            filePath,
            success: false,
            appliedEdits: i,
            error: `Edit ${i + 1}: oldString not found in file`,
          }
        }
      }

      await fs.writeFile(filePath, modified)
      return { filePath, success: true, appliedEdits: job.edits.length }
    }

    const result = await executeMultiEditJobs(jobs, applyJob, getConcurrency(input))
    const output = formatOutput(result)

    if (result.success) {
      return { success: true, output }
    }

    return {
      success: false,
      output,
      error: isSingle(input) ? primaryError(result) : `${result.failed} file edit(s) failed`,
      metadata: { results: result.results },
    }
  },
})
