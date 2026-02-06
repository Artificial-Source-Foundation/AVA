/**
 * Policy Matcher Utilities
 * Stable JSON stringify, wildcard tool patterns, regex args matching
 */

// ============================================================================
// Stable JSON Stringify
// ============================================================================

/**
 * Stable JSON stringify with sorted keys for deterministic regex matching.
 * Handles circular references and non-serializable values.
 */
export function stableStringify(value: unknown): string {
  const seen = new WeakSet()

  function serialize(val: unknown): string {
    if (val === null) return 'null'
    if (val === undefined) return 'undefined'
    if (typeof val === 'string') return JSON.stringify(val)
    if (typeof val === 'number' || typeof val === 'boolean') return String(val)
    if (typeof val === 'function') return '"[Function]"'

    if (typeof val === 'object') {
      if (seen.has(val as object)) return '"[Circular]"'
      seen.add(val as object)

      if (Array.isArray(val)) {
        const items = val.map((item) => serialize(item))
        return `[${items.join(',')}]`
      }

      const obj = val as Record<string, unknown>
      const keys = Object.keys(obj).sort()
      const entries = keys.map((key) => `${JSON.stringify(key)}:${serialize(obj[key])}`)
      return `{${entries.join(',')}}`
    }

    return String(val)
  }

  return serialize(value)
}

// ============================================================================
// Tool Name Matching
// ============================================================================

/**
 * Match tool name against a pattern with wildcard support.
 *
 * Patterns:
 * - `'*'` matches any tool
 * - `'mcp__*'` matches any MCP tool (e.g., mcp__github__search)
 * - `'delegate_*'` matches delegate_coder, delegate_tester, etc.
 * - `'bash'` matches only bash (exact match)
 *
 * Security: Prevents server spoofing by validating prefix boundaries.
 * For `mcp__*`, only matches tools where the MCP server name is an
 * exact prefix (not a substring of a malicious server name).
 */
export function matchToolName(pattern: string, toolName: string): boolean {
  // Exact match
  if (pattern === toolName) return true

  // Universal wildcard
  if (pattern === '*') return true

  // Prefix wildcard (e.g., 'mcp__*', 'delegate_*')
  if (pattern.endsWith('*')) {
    const prefix = pattern.slice(0, -1)
    if (!toolName.startsWith(prefix)) return false

    // Security: For MCP tools, validate server boundary
    // 'mcp__github__*' should match 'mcp__github__search'
    // but NOT 'mcp__github_malicious__tool'
    if (prefix.includes('__') && prefix.endsWith('__')) {
      const serverName = prefix.slice(0, -2) // Remove trailing '__'
      const toolPrefix = toolName.split('__').slice(0, serverName.split('__').length).join('__')
      return toolPrefix === serverName
    }

    return true
  }

  return false
}

// ============================================================================
// Args Matching
// ============================================================================

/**
 * Match tool arguments against a regex pattern.
 * Uses stable JSON stringify for order-independent matching.
 */
export function matchArgs(pattern: RegExp, args: Record<string, unknown>): boolean {
  const serialized = stableStringify(args)
  return pattern.test(serialized)
}

// ============================================================================
// Compound Command Checking
// ============================================================================

/** Operators that chain shell commands
 * @internal Reserved for future explicit operator handling
 */
// const COMPOUND_OPERATORS = ['&&', '||', '|', ';'] as const

/**
 * Check a compound shell command by splitting on operators
 * and checking each sub-command recursively.
 *
 * Decision aggregation (pessimistic):
 * - Any DENY → DENY (security first)
 * - Any ASK_USER → ASK_USER
 * - All ALLOW → ALLOW
 *
 * Redirections (>, >>, <) downgrade ALLOW → ASK_USER.
 */
export function checkCompoundCommand(
  command: string,
  checkFn: (subcommand: string) => 'allow' | 'deny' | 'ask_user'
): 'allow' | 'deny' | 'ask_user' {
  const trimmed = command.trim()

  // Check for redirections
  const hasRedirection = /[^|]>[^|]|>>|<(?!<)/.test(trimmed)

  // Split on compound operators (respecting quotes)
  const segments = splitCommand(trimmed)

  let hasAskUser = false

  for (const segment of segments) {
    const decision = checkFn(segment.trim())

    if (decision === 'deny') {
      return 'deny'
    }

    if (decision === 'ask_user') {
      hasAskUser = true
    }
  }

  // Redirections downgrade ALLOW → ASK_USER
  if (hasRedirection && !hasAskUser) {
    hasAskUser = true
  }

  return hasAskUser ? 'ask_user' : 'allow'
}

/**
 * Split a command string on compound operators, respecting quotes.
 */
function splitCommand(command: string): string[] {
  const segments: string[] = []
  let current = ''
  let inSingle = false
  let inDouble = false
  let escaped = false

  for (let i = 0; i < command.length; i++) {
    const char = command[i]!
    const next = command[i + 1]

    if (escaped) {
      current += char
      escaped = false
      continue
    }

    if (char === '\\') {
      escaped = true
      current += char
      continue
    }

    if (char === "'" && !inDouble) {
      inSingle = !inSingle
      current += char
      continue
    }

    if (char === '"' && !inSingle) {
      inDouble = !inDouble
      current += char
      continue
    }

    if (!inSingle && !inDouble) {
      // Check for compound operators
      const twoChar = char + (next ?? '')

      if (twoChar === '&&' || twoChar === '||') {
        segments.push(current)
        current = ''
        i++ // Skip next char
        continue
      }

      if (char === '|' || char === ';') {
        segments.push(current)
        current = ''
        continue
      }
    }

    current += char
  }

  if (current.trim()) {
    segments.push(current)
  }

  return segments.filter((s) => s.trim().length > 0)
}

/**
 * Extract the base command name from a command string.
 */
export function extractCommandName(command: string): string {
  const trimmed = command.trim()

  // Skip environment variables (e.g., "FOO=bar command")
  let start = 0
  while (start < trimmed.length) {
    const match = trimmed.slice(start).match(/^[A-Za-z_]\w*=\S*\s+/)
    if (!match) break
    start += match[0].length
  }

  // Extract first word
  const rest = trimmed.slice(start)
  const spaceIdx = rest.search(/\s/)
  return spaceIdx === -1 ? rest : rest.slice(0, spaceIdx)
}
