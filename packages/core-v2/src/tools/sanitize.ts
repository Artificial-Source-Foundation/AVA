/**
 * Content sanitization — strip markdown fences, normalize line endings.
 */

export interface SanitizeOptions {
  modelId?: string
  stripFences?: boolean
  normalizeLineEndings?: boolean
  trimTrailingWhitespace?: boolean
  ensureTrailingNewline?: boolean
}

export function sanitizeContent(content: string, options?: SanitizeOptions): string {
  let result = content

  if (options?.stripFences !== false) {
    result = stripMarkdownFences(result)
  }
  if (options?.normalizeLineEndings !== false) {
    result = normalizeLineEndings(result)
  }
  if (options?.ensureTrailingNewline !== false) {
    result = ensureTrailingNewline(result)
  }

  return result
}

export function stripMarkdownFences(content: string): string {
  const lines = content.split('\n')
  if (lines.length < 2) return content

  // Check if content starts with ```<lang> and ends with ```
  const firstLine = lines[0].trim()
  const lastLine = lines[lines.length - 1].trim()

  if (firstLine.startsWith('```') && lastLine === '```') {
    return lines.slice(1, -1).join('\n')
  }
  return content
}

export function normalizeLineEndings(content: string): string {
  return content.replace(/\r\n/g, '\n')
}

export function ensureTrailingNewline(content: string): string {
  if (content.length > 0 && !content.endsWith('\n')) {
    return `${content}\n`
  }
  return content
}

export function hasMarkdownFences(content: string): boolean {
  const lines = content.split('\n')
  if (lines.length < 2) return false
  return lines[0].trim().startsWith('```') && lines[lines.length - 1].trim() === '```'
}
