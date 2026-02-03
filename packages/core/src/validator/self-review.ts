/**
 * Self-Review Validator
 * Uses LLM to review code changes for issues
 *
 * Examines unified diffs and checks for:
 * - Bugs and logic errors
 * - Security vulnerabilities
 * - Edge cases not handled
 * - Performance issues
 * - Style violations
 */

import { createDiff } from '../diff/unified.js'
import { createClient, type LLMClient } from '../llm/client.js'
import { getPlatform } from '../platform.js'
import type { ChatMessage, ProviderConfig } from '../types/llm.js'
import { createFailedResult, createPassedResult } from './pipeline.js'
import type {
  SelfReviewConfig,
  SelfReviewIssue,
  ValidationContext,
  ValidationResult,
  Validator,
} from './types.js'

// ============================================================================
// Self-Review Validator
// ============================================================================

/**
 * Default self-review configuration
 */
const DEFAULT_SELF_REVIEW_CONFIG: SelfReviewConfig = {
  maxTokens: 2000,
  focus: ['bugs', 'security', 'edge-cases'],
}

/**
 * Self-Review Validator
 *
 * Uses LLM to review code changes and identify potential issues.
 * Non-critical by default - issues are reported but don't block.
 * Critical issues can be configured to block if needed.
 */
export const selfReviewValidator: Validator = {
  name: 'self-review',
  description: 'LLM-powered code review for bugs, security, and edge cases',
  critical: false, // Can be overridden for stricter review

  async canRun(_ctx: ValidationContext): Promise<boolean> {
    // Self-review requires LLM access
    // For now, always return true and handle errors gracefully
    return true
  },

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const startTime = Date.now()

    try {
      // Get diffs for the changed files
      const diffs = await collectDiffs(ctx.files, ctx.cwd)

      if (!diffs || diffs.trim().length === 0) {
        return createPassedResult('self-review', Date.now() - startTime, ['No changes to review'])
      }

      // Run LLM review
      const issues = await runLLMReview(diffs, ctx)

      const durationMs = Date.now() - startTime

      // Separate critical and minor issues
      const criticalIssues = issues.filter((i) => i.severity === 'critical')
      const minorIssues = issues.filter((i) => i.severity === 'minor')

      // Format issues for output
      const errors = criticalIssues.map(formatIssue)
      const warnings = minorIssues.map(formatIssue)

      if (criticalIssues.length > 0) {
        return createFailedResult('self-review', durationMs, errors, warnings)
      }

      return createPassedResult('self-review', durationMs, warnings)
    } catch (error) {
      // Self-review errors should not block - just warn
      return createPassedResult('self-review', Date.now() - startTime, [
        `Self-review failed: ${error}`,
      ])
    }
  },
}

// ============================================================================
// Diff Collection
// ============================================================================

/**
 * Collect diffs for changed files
 *
 * Uses git diff if available, otherwise compares against empty string
 */
async function collectDiffs(files: string[], cwd: string): Promise<string> {
  const shell = getPlatform().shell
  const diffs: string[] = []

  // Try to get diffs from git
  try {
    // Check if we're in a git repo
    const gitCheck = await shell.exec('git rev-parse --git-dir 2>&1', { cwd })

    if (gitCheck.exitCode === 0) {
      // Get staged + unstaged diffs
      const diffResult = await shell.exec('git diff HEAD 2>&1', { cwd })

      if (diffResult.exitCode === 0 && diffResult.stdout.trim()) {
        return diffResult.stdout
      }

      // If no HEAD diff, try just unstaged
      const unstagedResult = await shell.exec('git diff 2>&1', { cwd })
      if (unstagedResult.exitCode === 0 && unstagedResult.stdout.trim()) {
        return unstagedResult.stdout
      }
    }
  } catch {
    // Git not available, fall through to manual diff
  }

  // Manual diff: compare file contents (limited to provided files)
  const fs = getPlatform().fs

  for (const file of files.slice(0, 10)) {
    // Limit to 10 files
    try {
      const content = await fs.readFile(file)
      // Create diff against empty (shows entire file as added)
      const diff = createDiff(file, '', content, 3)
      diffs.push(diff)
    } catch {
      // Skip files that can't be read
    }
  }

  return diffs.join('\n')
}

// ============================================================================
// LLM Review
// ============================================================================

/**
 * Review prompt for the LLM
 */
const REVIEW_SYSTEM_PROMPT = `You are a code reviewer. Analyze the following code changes (unified diff format) and identify potential issues.

Focus on:
1. **Bugs**: Logic errors, incorrect behavior, runtime errors
2. **Security**: Vulnerabilities, injection risks, data exposure
3. **Edge cases**: Unhandled scenarios, boundary conditions
4. **Performance**: Inefficient operations, N+1 queries, memory leaks

For each issue found, respond with a JSON array of objects:
[
  {
    "severity": "critical" | "minor",
    "category": "bug" | "security" | "edge-case" | "performance" | "style",
    "description": "Brief description of the issue",
    "file": "path/to/file (if known)",
    "line": number (if known, use 0 if unknown),
    "suggestion": "How to fix it (optional)"
  }
]

If no issues are found, respond with an empty array: []

Be concise and focus on real issues. Don't flag trivial style issues unless they impact readability significantly.`

/**
 * Run LLM review on the diffs
 */
async function runLLMReview(diffs: string, ctx: ValidationContext): Promise<SelfReviewIssue[]> {
  // Truncate diffs if too long (keep under ~8000 tokens)
  const maxDiffChars = 30000 // ~7500 tokens
  const truncatedDiffs =
    diffs.length > maxDiffChars ? `${diffs.slice(0, maxDiffChars)}\n\n... (diff truncated)` : diffs

  // Create messages
  const messages: ChatMessage[] = [
    { role: 'user', content: `Review these code changes:\n\n${truncatedDiffs}` },
  ]

  // Create LLM client
  let client: LLMClient
  try {
    client = await createClient('anthropic')
  } catch {
    // Try fallback
    try {
      client = await createClient('openrouter')
    } catch {
      throw new Error('No LLM provider available for self-review')
    }
  }

  // Configure provider
  const providerConfig: ProviderConfig = {
    provider: 'anthropic',
    authMethod: 'api-key',
    model: 'claude-sonnet-4-20250514', // Fast, capable model
    maxTokens: DEFAULT_SELF_REVIEW_CONFIG.maxTokens!,
    systemPrompt: REVIEW_SYSTEM_PROMPT,
  }

  // Collect response
  let response = ''
  try {
    for await (const delta of client.stream(messages, providerConfig, ctx.signal)) {
      if (delta.content) {
        response += delta.content
      }
    }
  } catch (error) {
    // Handle streaming errors
    if (String(error).includes('abort')) {
      throw new Error('Review aborted')
    }
    throw error
  }

  // Parse response
  return parseReviewResponse(response)
}

/**
 * Parse LLM review response into issues
 */
function parseReviewResponse(response: string): SelfReviewIssue[] {
  try {
    // Try to extract JSON from response
    const jsonMatch = response.match(/\[[\s\S]*\]/)
    if (!jsonMatch) {
      return []
    }

    const parsed = JSON.parse(jsonMatch[0])

    if (!Array.isArray(parsed)) {
      return []
    }

    // Validate and normalize issues
    return parsed
      .filter((item): item is Record<string, unknown> => typeof item === 'object' && item !== null)
      .map((item) => ({
        severity: item.severity === 'critical' ? 'critical' : ('minor' as const),
        category: validateCategory(item.category),
        description: String(item.description || 'Unknown issue'),
        file: typeof item.file === 'string' ? item.file : undefined,
        line: typeof item.line === 'number' ? item.line : undefined,
        suggestion: typeof item.suggestion === 'string' ? item.suggestion : undefined,
      }))
  } catch {
    // JSON parsing failed - try to extract issues from text
    return parseTextResponse(response)
  }
}

/**
 * Validate issue category
 */
function validateCategory(
  category: unknown
): 'bug' | 'security' | 'edge-case' | 'performance' | 'style' {
  const validCategories = ['bug', 'security', 'edge-case', 'performance', 'style']
  if (typeof category === 'string' && validCategories.includes(category)) {
    return category as 'bug' | 'security' | 'edge-case' | 'performance' | 'style'
  }
  return 'bug' // Default
}

/**
 * Parse text response when JSON fails
 */
function parseTextResponse(response: string): SelfReviewIssue[] {
  const issues: SelfReviewIssue[] = []

  // Look for patterns like "Critical:" or "Issue:"
  const lines = response.split('\n')
  for (const line of lines) {
    const trimmed = line.trim()

    if (trimmed.toLowerCase().includes('critical')) {
      issues.push({
        severity: 'critical',
        category: 'bug',
        description: trimmed,
      })
    } else if (
      trimmed.toLowerCase().includes('security') ||
      trimmed.toLowerCase().includes('vulnerability')
    ) {
      issues.push({
        severity: 'critical',
        category: 'security',
        description: trimmed,
      })
    } else if (
      trimmed.toLowerCase().includes('issue') ||
      trimmed.toLowerCase().includes('warning') ||
      trimmed.toLowerCase().includes('bug')
    ) {
      issues.push({
        severity: 'minor',
        category: 'bug',
        description: trimmed,
      })
    }
  }

  return issues
}

// ============================================================================
// Formatting
// ============================================================================

/**
 * Format an issue for output
 */
function formatIssue(issue: SelfReviewIssue): string {
  let message = `[${issue.category}] ${issue.description}`

  if (issue.file) {
    message = `${issue.file}${issue.line ? `:${issue.line}` : ''} - ${message}`
  }

  if (issue.suggestion) {
    message += ` (Suggestion: ${issue.suggestion})`
  }

  return message
}

// ============================================================================
// Export
// ============================================================================

export default selfReviewValidator
