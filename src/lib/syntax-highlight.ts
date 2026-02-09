/**
 * Lightweight Syntax Highlighter
 *
 * Regex-based highlighting for common languages.
 * Outputs <span class="syn-*"> that map to --syntax-* CSS variables.
 * No external deps — keeps bundle small.
 */

// ============================================================================
// Types
// ============================================================================

interface LanguageRules {
  keywords?: string[]
  types?: string[]
  builtins?: string[]
  patterns: Array<{ pattern: RegExp; className: string }>
}

// ============================================================================
// Helpers
// ============================================================================

function escapeHtml(str: string): string {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
}

function keywordPattern(words: string[]): RegExp {
  return new RegExp(`\\b(${words.join('|')})\\b`, 'g')
}

// ============================================================================
// Language Definitions
// ============================================================================

const COMMENT_LINE: [RegExp, string] = [/(\/\/.*$|#.*$)/gm, 'syn-comment']
const COMMENT_BLOCK: [RegExp, string] = [/(\/\*[\s\S]*?\*\/)/g, 'syn-comment']
const STRING_DOUBLE: [RegExp, string] = [/("(?:[^"\\]|\\.)*")/g, 'syn-string']
const STRING_SINGLE: [RegExp, string] = [/('(?:[^'\\]|\\.)*')/g, 'syn-string']
const STRING_TEMPLATE: [RegExp, string] = [/(`(?:[^`\\]|\\.)*`)/g, 'syn-string']
const NUMBER: [RegExp, string] = [
  /\b(\d+\.?\d*(?:e[+-]?\d+)?|0x[\da-f]+|0b[01]+|0o[0-7]+)\b/gi,
  'syn-number',
]

const JS_KEYWORDS = [
  'async',
  'await',
  'break',
  'case',
  'catch',
  'class',
  'const',
  'continue',
  'debugger',
  'default',
  'delete',
  'do',
  'else',
  'export',
  'extends',
  'finally',
  'for',
  'from',
  'function',
  'if',
  'import',
  'in',
  'instanceof',
  'let',
  'new',
  'of',
  'return',
  'static',
  'super',
  'switch',
  'this',
  'throw',
  'try',
  'typeof',
  'var',
  'void',
  'while',
  'with',
  'yield',
]

const JS_TYPES = [
  'string',
  'number',
  'boolean',
  'object',
  'symbol',
  'bigint',
  'undefined',
  'null',
  'void',
  'never',
  'any',
  'unknown',
  'Array',
  'Promise',
  'Map',
  'Set',
  'Record',
  'Partial',
  'Required',
  'Readonly',
  'Pick',
  'Omit',
]

const languages: Record<string, LanguageRules> = {
  javascript: {
    keywords: JS_KEYWORDS,
    types: JS_TYPES,
    patterns: [
      { pattern: COMMENT_BLOCK[0], className: COMMENT_BLOCK[1] },
      { pattern: COMMENT_LINE[0], className: COMMENT_LINE[1] },
      { pattern: STRING_TEMPLATE[0], className: STRING_TEMPLATE[1] },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: STRING_SINGLE[0], className: STRING_SINGLE[1] },
      { pattern: NUMBER[0], className: NUMBER[1] },
      { pattern: /\b(true|false|null|undefined|NaN|Infinity)\b/g, className: 'syn-number' },
      { pattern: /\b([A-Z]\w*)\b/g, className: 'syn-type' },
      { pattern: /\b(\w+)(?=\s*\()/g, className: 'syn-function' },
    ],
  },
  python: {
    keywords: [
      'and',
      'as',
      'assert',
      'async',
      'await',
      'break',
      'class',
      'continue',
      'def',
      'del',
      'elif',
      'else',
      'except',
      'finally',
      'for',
      'from',
      'global',
      'if',
      'import',
      'in',
      'is',
      'lambda',
      'not',
      'or',
      'pass',
      'raise',
      'return',
      'try',
      'while',
      'with',
      'yield',
    ],
    patterns: [
      { pattern: /("""[\s\S]*?"""|'''[\s\S]*?''')/g, className: 'syn-string' },
      { pattern: /(#.*$)/gm, className: 'syn-comment' },
      { pattern: /(@\w+)/g, className: 'syn-function' },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: STRING_SINGLE[0], className: STRING_SINGLE[1] },
      { pattern: /(f"(?:[^"\\]|\\.)*"|f'(?:[^'\\]|\\.)*')/g, className: 'syn-string' },
      { pattern: NUMBER[0], className: NUMBER[1] },
      { pattern: /\b(True|False|None)\b/g, className: 'syn-number' },
      {
        pattern: /\b(int|str|float|bool|list|dict|tuple|set|bytes|type)\b/g,
        className: 'syn-type',
      },
      { pattern: /\b(\w+)(?=\s*\()/g, className: 'syn-function' },
    ],
  },
  rust: {
    keywords: [
      'as',
      'async',
      'await',
      'break',
      'const',
      'continue',
      'crate',
      'dyn',
      'else',
      'enum',
      'extern',
      'fn',
      'for',
      'if',
      'impl',
      'in',
      'let',
      'loop',
      'match',
      'mod',
      'move',
      'mut',
      'pub',
      'ref',
      'return',
      'self',
      'static',
      'struct',
      'super',
      'trait',
      'type',
      'unsafe',
      'use',
      'where',
      'while',
    ],
    types: [
      'i8',
      'i16',
      'i32',
      'i64',
      'i128',
      'u8',
      'u16',
      'u32',
      'u64',
      'u128',
      'f32',
      'f64',
      'bool',
      'char',
      'str',
      'String',
      'Vec',
      'Option',
      'Result',
      'Box',
      'Rc',
      'Arc',
      'HashMap',
      'HashSet',
      'usize',
      'isize',
    ],
    patterns: [
      { pattern: COMMENT_BLOCK[0], className: COMMENT_BLOCK[1] },
      { pattern: COMMENT_LINE[0], className: COMMENT_LINE[1] },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: NUMBER[0], className: NUMBER[1] },
      { pattern: /\b(true|false)\b/g, className: 'syn-number' },
      { pattern: /\b([A-Z]\w*)\b/g, className: 'syn-type' },
      { pattern: /\b(\w+)(?=\s*[!(])/g, className: 'syn-function' },
      { pattern: /(#\[[\w:]+\])/g, className: 'syn-function' },
    ],
  },
  go: {
    keywords: [
      'break',
      'case',
      'chan',
      'const',
      'continue',
      'default',
      'defer',
      'else',
      'fallthrough',
      'for',
      'func',
      'go',
      'goto',
      'if',
      'import',
      'interface',
      'map',
      'package',
      'range',
      'return',
      'select',
      'struct',
      'switch',
      'type',
      'var',
    ],
    types: [
      'int',
      'int8',
      'int16',
      'int32',
      'int64',
      'uint',
      'uint8',
      'uint16',
      'uint32',
      'uint64',
      'float32',
      'float64',
      'complex64',
      'complex128',
      'bool',
      'byte',
      'rune',
      'string',
      'error',
    ],
    patterns: [
      { pattern: COMMENT_BLOCK[0], className: COMMENT_BLOCK[1] },
      { pattern: COMMENT_LINE[0], className: COMMENT_LINE[1] },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: /(`[^`]*`)/g, className: 'syn-string' },
      { pattern: NUMBER[0], className: NUMBER[1] },
      { pattern: /\b(true|false|nil|iota)\b/g, className: 'syn-number' },
      { pattern: /\b(\w+)(?=\s*\()/g, className: 'syn-function' },
    ],
  },
  bash: {
    keywords: [
      'if',
      'then',
      'else',
      'elif',
      'fi',
      'for',
      'while',
      'do',
      'done',
      'case',
      'esac',
      'in',
      'function',
      'return',
      'local',
      'export',
      'readonly',
      'declare',
      'set',
      'unset',
      'shift',
      'exit',
      'source',
    ],
    patterns: [
      { pattern: /(#.*$)/gm, className: 'syn-comment' },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: STRING_SINGLE[0], className: STRING_SINGLE[1] },
      { pattern: /(\$\{?\w+\}?)/g, className: 'syn-variable' },
      { pattern: NUMBER[0], className: NUMBER[1] },
      { pattern: /\b(\w+)(?=\s*\()/g, className: 'syn-function' },
    ],
  },
  json: {
    patterns: [
      { pattern: /("(?:[^"\\]|\\.)*")(?=\s*:)/g, className: 'syn-keyword' },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: NUMBER[0], className: NUMBER[1] },
      { pattern: /\b(true|false|null)\b/g, className: 'syn-number' },
    ],
  },
  css: {
    patterns: [
      { pattern: COMMENT_BLOCK[0], className: COMMENT_BLOCK[1] },
      { pattern: /((?:\.|#|@)\w[\w-]*)/g, className: 'syn-keyword' },
      { pattern: /([\w-]+)(?=\s*:)/g, className: 'syn-function' },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: STRING_SINGLE[0], className: STRING_SINGLE[1] },
      { pattern: /(#[\da-f]{3,8})\b/gi, className: 'syn-number' },
      { pattern: /(\d+\.?\d*(?:px|em|rem|%|vh|vw|s|ms|deg|fr)?)\b/g, className: 'syn-number' },
    ],
  },
  html: {
    patterns: [
      { pattern: /(<!--[\s\S]*?-->)/g, className: 'syn-comment' },
      { pattern: /(<\/?)([\w-]+)/g, className: 'syn-keyword' },
      { pattern: /([\w-]+)(?==)/g, className: 'syn-function' },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: STRING_SINGLE[0], className: STRING_SINGLE[1] },
    ],
  },
  sql: {
    keywords: [
      'select',
      'from',
      'where',
      'and',
      'or',
      'not',
      'in',
      'is',
      'null',
      'like',
      'between',
      'join',
      'inner',
      'left',
      'right',
      'outer',
      'on',
      'as',
      'order',
      'by',
      'group',
      'having',
      'limit',
      'offset',
      'insert',
      'into',
      'values',
      'update',
      'set',
      'delete',
      'create',
      'table',
      'drop',
      'alter',
      'index',
      'primary',
      'key',
      'foreign',
      'references',
      'unique',
      'default',
      'exists',
      'union',
      'all',
      'distinct',
      'count',
      'sum',
      'avg',
      'min',
      'max',
      'case',
      'when',
      'then',
      'else',
      'end',
      'asc',
      'desc',
      'with',
      'recursive',
    ],
    types: [
      'int',
      'integer',
      'varchar',
      'text',
      'boolean',
      'date',
      'timestamp',
      'float',
      'double',
      'decimal',
      'bigint',
      'serial',
      'json',
      'jsonb',
      'uuid',
    ],
    patterns: [
      { pattern: /(--.*$)/gm, className: 'syn-comment' },
      { pattern: COMMENT_BLOCK[0], className: COMMENT_BLOCK[1] },
      { pattern: STRING_SINGLE[0], className: STRING_SINGLE[1] },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: NUMBER[0], className: NUMBER[1] },
      { pattern: /\b(true|false|null)\b/gi, className: 'syn-number' },
    ],
  },
  yaml: {
    patterns: [
      { pattern: /(#.*$)/gm, className: 'syn-comment' },
      { pattern: /^([\w.-]+)(?=\s*:)/gm, className: 'syn-keyword' },
      { pattern: STRING_DOUBLE[0], className: STRING_DOUBLE[1] },
      { pattern: STRING_SINGLE[0], className: STRING_SINGLE[1] },
      { pattern: NUMBER[0], className: NUMBER[1] },
      { pattern: /\b(true|false|null|yes|no)\b/gi, className: 'syn-number' },
    ],
  },
}

// Aliases
languages.js = languages.javascript
languages.ts = languages.javascript
languages.typescript = languages.javascript
languages.jsx = languages.javascript
languages.tsx = languages.javascript
languages.sh = languages.bash
languages.shell = languages.bash
languages.zsh = languages.bash
languages.yml = languages.yaml
languages.htm = languages.html
languages.xml = languages.html
languages.svg = languages.html
languages.toml = languages.yaml

// ============================================================================
// Highlighter
// ============================================================================

/**
 * Highlight code using regex patterns mapped to --syntax-* CSS vars.
 * Returns HTML string with <span class="syn-*"> wrappers.
 */
export function highlightCode(code: string, lang: string): string {
  const language = languages[lang.toLowerCase()]
  if (!language) return escapeHtml(code)

  // Phase 1: Escape HTML in original code
  let result = escapeHtml(code)

  // Phase 2: Build list of all replacements from patterns
  // We use a marker-based approach: find matches, replace with unique markers,
  // then swap markers for spans at the end. This prevents double-highlighting.
  const markers: Array<{ marker: string; replacement: string }> = []
  let markerIndex = 0

  // Apply keyword/type highlighting from word lists
  if (language.keywords) {
    const kw = keywordPattern(language.keywords)
    result = result.replace(kw, (match) => {
      const marker = `\x00KW${markerIndex++}\x00`
      markers.push({ marker, replacement: `<span class="syn-keyword">${match}</span>` })
      return marker
    })
  }

  if (language.types) {
    const tp = keywordPattern(language.types)
    result = result.replace(tp, (match) => {
      const marker = `\x00TP${markerIndex++}\x00`
      markers.push({ marker, replacement: `<span class="syn-type">${match}</span>` })
      return marker
    })
  }

  // Apply pattern-based highlighting
  for (const rule of language.patterns) {
    // Create a fresh regex each time (avoid lastIndex issues)
    const re = new RegExp(rule.pattern.source, rule.pattern.flags)
    result = result.replace(re, (match) => {
      // Don't highlight if already inside a marker
      if (match.includes('\x00')) return match
      const marker = `\x00PT${markerIndex++}\x00`
      markers.push({ marker, replacement: `<span class="${rule.className}">${match}</span>` })
      return marker
    })
  }

  // Phase 3: Replace markers with actual spans
  for (const { marker, replacement } of markers) {
    result = result.replace(marker, replacement)
  }

  return result
}
