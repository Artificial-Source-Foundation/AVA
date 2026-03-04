/**
 * Repo Map Tool — generate a project structure overview.
 *
 * Uses the codebase extension's file indexer to discover and categorize files.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import { detectLanguage, indexFiles } from '../../codebase/src/indexer.js'

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes}B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)}KB`
  return `${(bytes / (1024 * 1024)).toFixed(1)}MB`
}

export const repoMapTool = defineTool({
  name: 'repo_map',
  description: 'Generate project structure overview with file count and language breakdown.',

  schema: z.object({
    language: z.string().optional().describe('Filter by language (e.g., "typescript")'),
    maxFiles: z.number().optional().describe('Max files to show (default: 200)'),
  }),

  permissions: ['read'],

  async execute(input, ctx) {
    if (ctx.signal.aborted) {
      return { success: false, output: 'Operation was cancelled', error: 'EXECUTION_ABORTED' }
    }

    const fs = getPlatform().fs
    const maxFiles = input.maxFiles ?? 200

    try {
      // Use broad patterns to catch common source files
      const patterns = [
        '**/*.{ts,tsx,js,jsx,py,rs,go,java,rb,php,c,cpp,h,hpp,cs,swift,kt,scala}',
        '**/*.{vue,svelte,html,css,scss}',
        '**/*.{json,yaml,yml,toml,md,sh}',
      ]

      const files = await indexFiles(ctx.workingDirectory, fs, patterns)

      // Group by language
      const byLanguage = new Map<string, typeof files>()
      for (const file of files) {
        const lang = file.language ?? detectLanguage(file.path)
        if (input.language && lang !== input.language) continue
        const group = byLanguage.get(lang) ?? []
        group.push(file)
        byLanguage.set(lang, group)
      }

      // Build output
      const totalFiles = Array.from(byLanguage.values()).reduce((sum, g) => sum + g.length, 0)
      const lines: string[] = [`## Project Structure (${totalFiles} files)`]

      // Language breakdown
      lines.push(``, `### Language Breakdown`)
      const sortedLangs = Array.from(byLanguage.entries()).sort(
        ([, a], [, b]) => b.length - a.length
      )
      for (const [lang, group] of sortedLangs) {
        lines.push(`- ${lang}: ${group.length} files`)
      }

      // File listing
      lines.push(``, `### Files`)
      let shown = 0
      for (const [, group] of sortedLangs) {
        const sorted = group.sort((a, b) => a.path.localeCompare(b.path))
        for (const file of sorted) {
          if (shown >= maxFiles) break
          // Show relative path
          const relPath = file.path.startsWith(ctx.workingDirectory)
            ? file.path.slice(ctx.workingDirectory.length + 1)
            : file.path
          lines.push(`${relPath} (${formatSize(file.size)})`)
          shown++
        }
        if (shown >= maxFiles) break
      }

      if (totalFiles > maxFiles) {
        lines.push(``, `... and ${totalFiles - maxFiles} more files`)
      }

      if (ctx.metadata) {
        ctx.metadata({
          title: `Repo map: ${totalFiles} files`,
          metadata: { totalFiles, languages: sortedLangs.map(([l]) => l) },
        })
      }

      return {
        success: true,
        output: lines.join('\n'),
        metadata: {
          totalFiles,
          languages: Object.fromEntries(sortedLangs.map(([l, g]) => [l, g.length])),
        },
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Failed to generate repo map: ${message}`,
        error: 'REPO_MAP_FAILED',
      }
    }
  },
})
