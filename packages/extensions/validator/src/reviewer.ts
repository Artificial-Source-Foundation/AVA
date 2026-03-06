import type { ChatMessage, LLMProvider } from '@ava/core-v2/llm'
import { createClient } from '@ava/core-v2/llm'

export interface ReviewResult {
  approved: boolean
  feedback: string
  confidence: number
  issues: string[]
}

function extractJsonObject(text: string): string | null {
  const start = text.indexOf('{')
  const end = text.lastIndexOf('}')
  if (start === -1 || end === -1 || end <= start) return null
  return text.slice(start, end + 1)
}

function parseReviewResult(raw: string): ReviewResult {
  const json = extractJsonObject(raw)
  if (!json) {
    return {
      approved: false,
      feedback: 'Reviewer returned non-JSON output.',
      confidence: 0,
      issues: [raw.trim() || 'No review details provided'],
    }
  }

  try {
    const parsed = JSON.parse(json) as {
      approved?: unknown
      feedback?: unknown
      confidence?: unknown
      issues?: unknown
    }
    const issues = Array.isArray(parsed.issues)
      ? parsed.issues.filter((issue): issue is string => typeof issue === 'string')
      : []
    const confidence =
      typeof parsed.confidence === 'number' && Number.isFinite(parsed.confidence)
        ? Math.max(0, Math.min(1, parsed.confidence))
        : parsed.approved === true
          ? 0.8
          : 0.4

    return {
      approved: parsed.approved === true,
      feedback:
        typeof parsed.feedback === 'string'
          ? parsed.feedback
          : parsed.approved === true
            ? 'Approved'
            : 'Reviewer rejected output without detailed feedback.',
      confidence,
      issues,
    }
  } catch {
    return {
      approved: false,
      feedback: 'Reviewer JSON could not be parsed.',
      confidence: 0,
      issues: [raw.trim() || 'No review details provided'],
    }
  }
}

export async function reviewAgentOutput(
  goal: string,
  output: string,
  filesChanged: string[],
  diffs: string[],
  provider: LLMProvider,
  model: string,
  signal?: AbortSignal
): Promise<ReviewResult> {
  const client = createClient(provider)
  const reviewPrompt = [
    'You are a code reviewer. The agent was given this goal:',
    goal,
    '',
    'It produced these file changes:',
    filesChanged.length > 0 ? filesChanged.join('\n') : '(none provided)',
    '',
    'And these diffs:',
    diffs.length > 0 ? diffs.join('\n\n') : '(none provided)',
    '',
    'And this summary:',
    output,
    '',
    'Review for:',
    '1. Does the output satisfy the goal?',
    '2. Are there obvious bugs or missing edge cases?',
    '3. Are there test gaps?',
    '',
    'Respond with JSON only:',
    '{ "approved": boolean, "feedback": string, "confidence": number, "issues": string[] }',
  ].join('\n')

  const messages: ChatMessage[] = [
    {
      role: 'system',
      content:
        'You are a strict software review agent. Output JSON only. Do not include markdown fences.',
    },
    { role: 'user', content: reviewPrompt },
  ]

  let responseText = ''
  try {
    for await (const delta of client.stream(
      messages,
      {
        provider,
        model,
        maxTokens: 1000,
      },
      signal
    )) {
      if (delta.error) {
        return {
          approved: false,
          feedback: `Reviewer call failed: ${delta.error.message}`,
          confidence: 0,
          issues: [delta.error.message],
        }
      }
      if (delta.content) responseText += delta.content
    }
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    return {
      approved: false,
      feedback: `Reviewer stream failed: ${message}`,
      confidence: 0,
      issues: [message],
    }
  }

  return parseReviewResult(responseText)
}
