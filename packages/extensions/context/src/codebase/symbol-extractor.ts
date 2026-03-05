/**
 * Symbol extractor — regex-based extraction of functions, classes, interfaces, types.
 *
 * Supports TypeScript/JavaScript, Python, Rust, and Go.
 * Does not require tree-sitter — uses line-by-line regex scanning.
 */

import type { FileSymbol } from './types.js'

const MAX_FILE_SIZE = 500_000 // 500KB

interface SymbolPattern {
  regex: RegExp
  kind: FileSymbol['kind']
}

// ─── Language-specific patterns ─────────────────────────────────────────────

const TYPESCRIPT_PATTERNS: SymbolPattern[] = [
  { regex: /^(?:export\s+)?(?:async\s+)?function\s+(\w+)/m, kind: 'function' },
  { regex: /^(?:export\s+)?(?:abstract\s+)?class\s+(\w+)/m, kind: 'class' },
  { regex: /^(?:export\s+)?interface\s+(\w+)/m, kind: 'interface' },
  { regex: /^(?:export\s+)?type\s+(\w+)\s*[=<]/m, kind: 'type' },
  { regex: /^(?:export\s+)?enum\s+(\w+)/m, kind: 'enum' },
  { regex: /^(?:export\s+)?(?:const|let|var)\s+(\w+)\s*[:=]/m, kind: 'variable' },
  // Methods in class bodies (indented)
  { regex: /^\s+(?:async\s+)?(?:static\s+)?(?:readonly\s+)?(\w+)\s*\(/m, kind: 'method' },
]

const PYTHON_PATTERNS: SymbolPattern[] = [
  { regex: /^(?:async\s+)?def\s+(\w+)/m, kind: 'function' },
  { regex: /^class\s+(\w+)/m, kind: 'class' },
  // Methods (indented def)
  { regex: /^\s+(?:async\s+)?def\s+(\w+)/m, kind: 'method' },
]

const RUST_PATTERNS: SymbolPattern[] = [
  { regex: /^(?:pub\s+)?(?:async\s+)?fn\s+(\w+)/m, kind: 'function' },
  { regex: /^(?:pub\s+)?struct\s+(\w+)/m, kind: 'class' },
  { regex: /^(?:pub\s+)?trait\s+(\w+)/m, kind: 'interface' },
  { regex: /^(?:pub\s+)?enum\s+(\w+)/m, kind: 'enum' },
  { regex: /^(?:pub\s+)?type\s+(\w+)/m, kind: 'type' },
  // Impl methods
  { regex: /^\s+(?:pub\s+)?(?:async\s+)?fn\s+(\w+)/m, kind: 'method' },
]

const GO_PATTERNS: SymbolPattern[] = [
  { regex: /^func\s+(\w+)/m, kind: 'function' },
  { regex: /^func\s+\([^)]+\)\s+(\w+)/m, kind: 'method' },
  { regex: /^type\s+(\w+)\s+struct/m, kind: 'class' },
  { regex: /^type\s+(\w+)\s+interface/m, kind: 'interface' },
  { regex: /^type\s+(\w+)\s+/m, kind: 'type' },
  { regex: /^var\s+(\w+)\s+/m, kind: 'variable' },
]

const LANGUAGE_PATTERNS: Record<string, SymbolPattern[]> = {
  typescript: TYPESCRIPT_PATTERNS,
  javascript: TYPESCRIPT_PATTERNS,
  python: PYTHON_PATTERNS,
  rust: RUST_PATTERNS,
  go: GO_PATTERNS,
}

// Skip common non-symbol identifiers in TypeScript/JS
const SKIP_NAMES = new Set([
  'if',
  'else',
  'for',
  'while',
  'switch',
  'case',
  'return',
  'throw',
  'try',
  'catch',
  'finally',
  'new',
  'delete',
  'typeof',
  'void',
  'break',
  'continue',
  'do',
  'in',
  'of',
  'with',
  'yield',
  'await',
  'constructor',
  'super',
  'this',
  'import',
  'from',
  'as',
])

/**
 * Extract symbols from a source file.
 * Returns empty array for unsupported languages or files > 500KB.
 */
export function extractSymbols(content: string, language: string, filePath: string): FileSymbol[] {
  if (content.length > MAX_FILE_SIZE) return []

  const patterns = LANGUAGE_PATTERNS[language]
  if (!patterns) return []

  const symbols: FileSymbol[] = []
  const lines = content.split('\n')
  const seen = new Set<string>()

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]!
    for (const pattern of patterns) {
      const match = pattern.regex.exec(line)
      if (match?.[1] && !SKIP_NAMES.has(match[1])) {
        const key = `${match[1]}:${pattern.kind}:${i}`
        if (!seen.has(key)) {
          seen.add(key)
          symbols.push({
            name: match[1],
            kind: pattern.kind,
            line: i + 1,
            filePath,
          })
        }
      }
    }
  }

  return symbols
}

/** Get supported languages for symbol extraction. */
export function getSupportedLanguages(): string[] {
  return Object.keys(LANGUAGE_PATTERNS)
}
