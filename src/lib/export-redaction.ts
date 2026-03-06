/**
 * Redaction helpers for conversation export.
 * Strips API keys, file paths, and emails from text.
 */

export interface RedactionOptions {
  stripApiKeys: boolean
  stripFilePaths: boolean
  stripEmails: boolean
}

const API_KEY_PATTERNS = [
  /\bsk-[a-zA-Z0-9_-]{20,}\b/g, // OpenAI, Anthropic
  /\bkey-[a-zA-Z0-9_-]{20,}\b/g, // Generic key-prefixed
  /\bBearer\s+[a-zA-Z0-9._-]{20,}\b/g, // Bearer tokens
  /\bghp_[a-zA-Z0-9]{36,}\b/g, // GitHub PAT
  /\bgho_[a-zA-Z0-9]{36,}\b/g, // GitHub OAuth
  /\bxoxb-[a-zA-Z0-9-]+\b/g, // Slack bot
  /\bAIza[a-zA-Z0-9_-]{35}\b/g, // Google API
  /\bAKIA[A-Z0-9]{16}\b/g, // AWS access key
]

const FILE_PATH_PATTERNS = [
  /(?:\/(?:home|Users|root|var|tmp|etc|opt|usr)\/)[^\s"'`),;]+/g, // Unix absolute
  /[A-Z]:\\[^\s"'`),;]+/g, // Windows absolute
]

const EMAIL_PATTERN = /\b[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}\b/g

export function applyRedaction(text: string, options: RedactionOptions): string {
  let result = text
  if (options.stripApiKeys) {
    for (const pattern of API_KEY_PATTERNS) {
      result = result.replace(pattern, '[REDACTED_KEY]')
    }
  }
  if (options.stripFilePaths) {
    for (const pattern of FILE_PATH_PATTERNS) {
      result = result.replace(pattern, '[REDACTED_PATH]')
    }
  }
  if (options.stripEmails) {
    result = result.replace(EMAIL_PATTERN, '[REDACTED_EMAIL]')
  }
  return result
}
