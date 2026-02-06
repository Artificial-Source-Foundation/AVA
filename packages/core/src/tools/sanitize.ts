/**
 * Content Sanitization
 * Cleans LLM output before writing to files
 *
 * Handles common issues:
 * - Markdown code fences (common with Gemini, DeepSeek, Llama)
 * - Model-specific encoding quirks
 * - Trailing whitespace and newlines
 */

// ============================================================================
// Types
// ============================================================================

/**
 * Detected model family for model-specific fixes
 */
export type SanitizeModelFamily =
  | 'claude'
  | 'gpt'
  | 'gemini'
  | 'deepseek'
  | 'llama'
  | 'mistral'
  | 'unknown'

/**
 * Options for content sanitization
 */
export interface SanitizeOptions {
  /** Model ID or family for model-specific fixes */
  modelId?: string
  /** Whether to strip markdown fences (default: true) */
  stripFences?: boolean
  /** Whether to normalize line endings (default: true) */
  normalizeLineEndings?: boolean
  /** Whether to trim trailing whitespace (default: false) */
  trimTrailingWhitespace?: boolean
  /** Whether to ensure trailing newline (default: true) */
  ensureTrailingNewline?: boolean
}

const DEFAULT_OPTIONS: Required<SanitizeOptions> = {
  modelId: '',
  stripFences: true,
  normalizeLineEndings: true,
  trimTrailingWhitespace: false,
  ensureTrailingNewline: true,
}

// ============================================================================
// Model Detection
// ============================================================================

/**
 * Detect model family from model ID for sanitization
 */
export function detectSanitizeModelFamily(modelId: string): SanitizeModelFamily {
  const id = modelId.toLowerCase()

  if (id.includes('claude') || id.includes('anthropic')) return 'claude'
  if (id.includes('gpt') || id.includes('openai') || id.includes('o1') || id.includes('o3'))
    return 'gpt'
  if (id.includes('gemini') || id.includes('google')) return 'gemini'
  if (id.includes('deepseek')) return 'deepseek'
  if (id.includes('llama') || id.includes('meta')) return 'llama'
  if (id.includes('mistral') || id.includes('mixtral')) return 'mistral'

  return 'unknown'
}

// ============================================================================
// Fence Stripping
// ============================================================================

/**
 * Language patterns commonly seen in markdown fences
 */
const FENCE_LANGUAGES = [
  'typescript',
  'javascript',
  'ts',
  'js',
  'tsx',
  'jsx',
  'python',
  'py',
  'rust',
  'rs',
  'go',
  'java',
  'c',
  'cpp',
  'c\\+\\+',
  'csharp',
  'cs',
  'ruby',
  'rb',
  'php',
  'swift',
  'kotlin',
  'scala',
  'shell',
  'bash',
  'sh',
  'zsh',
  'fish',
  'powershell',
  'ps1',
  'sql',
  'html',
  'css',
  'scss',
  'sass',
  'less',
  'json',
  'yaml',
  'yml',
  'toml',
  'xml',
  'markdown',
  'md',
  'plaintext',
  'text',
  'txt',
  '',
]

/**
 * Regex to match opening markdown fence with optional language
 */
const FENCE_OPEN_REGEX = new RegExp(`^\\s*\`\`\`(${FENCE_LANGUAGES.join('|')})?\\s*$`, 'im')

/**
 * Regex to match closing markdown fence
 */
const FENCE_CLOSE_REGEX = /^\s*```\s*$/m

/**
 * Strip markdown code fences from content
 *
 * Handles cases like:
 * ```typescript
 * const x = 1
 * ```
 *
 * Returns just:
 * const x = 1
 */
export function stripMarkdownFences(content: string): string {
  let result = content.trim()

  // Check if content starts with fence
  const startMatch = result.match(FENCE_OPEN_REGEX)
  if (startMatch && result.startsWith(startMatch[0].trim())) {
    // Remove opening fence
    result = result.slice(startMatch[0].length)
  }

  // Check if content ends with fence
  if (FENCE_CLOSE_REGEX.test(result)) {
    // Find and remove closing fence (iterate backwards for findLastIndex compatibility)
    const lines = result.split('\n')
    let lastFenceIndex = -1
    for (let i = lines.length - 1; i >= 0; i--) {
      if (FENCE_CLOSE_REGEX.test(lines[i])) {
        lastFenceIndex = i
        break
      }
    }
    if (lastFenceIndex !== -1) {
      lines.splice(lastFenceIndex, 1)
      result = lines.join('\n')
    }
  }

  return result.trim()
}

// ============================================================================
// Model-Specific Fixes
// ============================================================================

/**
 * Apply Gemini-specific content fixes
 */
function fixGeminiContent(content: string): string {
  let result = content

  // Gemini sometimes escapes newlines as literal \n
  result = result.replace(/\\n/g, '\n')

  // Gemini sometimes double-escapes backslashes
  result = result.replace(/\\\\/g, '\\')

  return result
}

/**
 * Apply DeepSeek-specific content fixes
 */
function fixDeepSeekContent(content: string): string {
  let result = content

  // DeepSeek sometimes uses HTML entities in code
  result = result.replace(/&amp;&amp;/g, '&&')
  result = result.replace(/&amp;/g, '&')
  result = result.replace(/&lt;/g, '<')
  result = result.replace(/&gt;/g, '>')
  result = result.replace(/&quot;/g, '"')
  result = result.replace(/&#39;/g, "'")
  result = result.replace(/&nbsp;/g, ' ')

  // DeepSeek sometimes escapes single quotes oddly
  result = result.replace(/\\'/g, "'")

  return result
}

/**
 * Apply Llama-specific content fixes
 */
function fixLlamaContent(content: string): string {
  let result = content

  // Llama sometimes adds extra indentation
  const lines = result.split('\n')
  if (lines.length > 1) {
    // Check if all non-empty lines have consistent extra indentation
    const nonEmptyLines = lines.filter((l) => l.trim().length > 0)
    if (nonEmptyLines.length > 0) {
      const minIndent = Math.min(
        ...nonEmptyLines.map((l) => {
          const match = l.match(/^(\s*)/)
          return match ? match[1].length : 0
        })
      )

      // If minimum indent is suspiciously large (>= 4 spaces), reduce it
      if (minIndent >= 4) {
        result = lines.map((l) => (l.length >= minIndent ? l.slice(minIndent) : l)).join('\n')
      }
    }
  }

  return result
}

/**
 * Apply model-specific content fixes
 */
function applyModelFixes(content: string, modelFamily: SanitizeModelFamily): string {
  switch (modelFamily) {
    case 'gemini':
      return fixGeminiContent(content)
    case 'deepseek':
      return fixDeepSeekContent(content)
    case 'llama':
      return fixLlamaContent(content)
    default:
      return content
  }
}

// ============================================================================
// Line Ending Normalization
// ============================================================================

/**
 * Normalize line endings to LF
 */
export function normalizeLineEndings(content: string): string {
  // Replace CRLF with LF, then replace any remaining CR with LF
  return content.replace(/\r\n/g, '\n').replace(/\r/g, '\n')
}

/**
 * Trim trailing whitespace from each line
 */
export function trimTrailingWhitespace(content: string): string {
  return content
    .split('\n')
    .map((line) => line.trimEnd())
    .join('\n')
}

/**
 * Ensure content ends with exactly one newline
 */
export function ensureTrailingNewline(content: string): string {
  const trimmed = content.replace(/\n+$/, '')
  return `${trimmed}\n`
}

// ============================================================================
// Main Sanitization Function
// ============================================================================

/**
 * Sanitize content before writing to file
 *
 * @param content - Raw content from LLM
 * @param options - Sanitization options
 * @returns Cleaned content ready for writing
 *
 * @example
 * ```typescript
 * const clean = sanitizeContent('```typescript\nconst x = 1\n```', {
 *   modelId: 'gemini-pro',
 * })
 * // Returns: 'const x = 1\n'
 * ```
 */
export function sanitizeContent(content: string, options: SanitizeOptions = {}): string {
  const opts = { ...DEFAULT_OPTIONS, ...options }
  let result = content

  // 1. Detect model family
  const modelFamily = opts.modelId ? detectSanitizeModelFamily(opts.modelId) : 'unknown'

  // 2. Apply model-specific fixes first
  result = applyModelFixes(result, modelFamily)

  // 3. Strip markdown fences
  if (opts.stripFences) {
    result = stripMarkdownFences(result)
  }

  // 4. Normalize line endings
  if (opts.normalizeLineEndings) {
    result = normalizeLineEndings(result)
  }

  // 5. Trim trailing whitespace per line
  if (opts.trimTrailingWhitespace) {
    result = trimTrailingWhitespace(result)
  }

  // 6. Ensure trailing newline
  if (opts.ensureTrailingNewline) {
    result = ensureTrailingNewline(result)
  }

  return result
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if content appears to have markdown fences
 */
export function hasMarkdownFences(content: string): boolean {
  return FENCE_OPEN_REGEX.test(content) && FENCE_CLOSE_REGEX.test(content)
}

/**
 * Extract language from markdown fence if present
 */
export function extractFenceLanguage(content: string): string | null {
  const match = content.match(FENCE_OPEN_REGEX)
  if (match?.[1]) {
    return match[1].trim()
  }
  return null
}
