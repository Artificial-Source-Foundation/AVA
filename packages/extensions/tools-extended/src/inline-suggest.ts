import { createClient, hasProvider, type LLMProvider } from '@ava/core-v2/llm'
import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import { z } from 'zod'
import {
  buildInlineSuggestCacheKey,
  getCachedInlineSuggestion,
  setCachedInlineSuggestion,
} from './inline-suggest-cache.js'

const inlineSuggestSchema = z.object({
  path: z.string().describe('Path to file for inline completion.'),
  line: z.number().int().min(1).describe('1-based cursor line.'),
  column: z.number().int().min(1).describe('1-based cursor column.'),
  provider: z.string().optional().describe('Optional provider override.'),
  model: z.string().optional().describe('Optional model override.'),
  maxTokens: z.number().int().min(1).max(512).optional(),
})

function splitAtCursor(
  content: string,
  line: number,
  column: number
): { prefix: string; suffix: string } {
  const lines = content.split('\n')
  const safeLine = Math.min(Math.max(line, 1), Math.max(lines.length, 1))
  const lineIndex = safeLine - 1

  const current = lines[lineIndex] ?? ''
  const safeColumn = Math.min(Math.max(column, 1), current.length + 1)
  const colIndex = safeColumn - 1

  const prefixLines = lines.slice(0, lineIndex)
  prefixLines.push(current.slice(0, colIndex))
  const suffixLines = [current.slice(colIndex), ...lines.slice(lineIndex + 1)]

  return {
    prefix: prefixLines.join('\n'),
    suffix: suffixLines.join('\n'),
  }
}

function buildFimPrompt(prefix: string, suffix: string): string {
  return `<fim_prefix>${prefix}<fim_suffix>${suffix}<fim_middle>`
}

export const inlineSuggestTool = defineTool({
  name: 'inline_suggest',
  description: 'Generate inline completion using fill-in-the-middle prompting.',
  schema: inlineSuggestSchema,
  async execute(input, ctx) {
    const resolvedPath = await resolvePathSafe(input.path, ctx.workingDirectory)
    const provider = (input.provider ?? ctx.provider ?? 'anthropic') as LLMProvider
    const model = input.model ?? ctx.model ?? 'claude-3-5-sonnet-latest'

    if (!hasProvider(provider)) {
      return {
        success: false,
        output: '',
        error: `Provider '${provider}' is not registered`,
      }
    }

    const fileContent = await getPlatform().fs.readFile(resolvedPath)
    const { prefix, suffix } = splitAtCursor(fileContent, input.line, input.column)
    const cacheKey = buildInlineSuggestCacheKey({
      path: resolvedPath,
      line: input.line,
      column: input.column,
      provider,
      model,
      prefix,
      suffix,
    })

    const cached = getCachedInlineSuggestion(cacheKey)
    if (cached !== null) {
      return {
        success: true,
        output: cached,
        metadata: { cached: true },
      }
    }

    const client = createClient(provider)
    const prompt = buildFimPrompt(prefix, suffix)
    let completion = ''

    for await (const delta of client.stream(
      [{ role: 'user', content: prompt }],
      {
        provider,
        model,
        temperature: 0.2,
        maxTokens: input.maxTokens ?? 128,
      },
      ctx.signal
    )) {
      if (delta.content) {
        completion += delta.content
      }
    }

    const normalized = completion.trim()
    setCachedInlineSuggestion(cacheKey, normalized)

    return {
      success: true,
      output: normalized,
      metadata: {
        cached: false,
        provider,
        model,
      },
    }
  },
})
