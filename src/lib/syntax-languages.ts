/**
 * Syntax Highlighting — Language Definitions
 *
 * Regex-based rules and keyword lists for all supported languages.
 * Consumed by the highlightCode() function in syntax-highlight.ts.
 *
 * Additional language definitions (Rust, Go, SQL) are in syntax-languages-extra.ts.
 */

import { extraLanguages } from './syntax-languages-extra'

// ============================================================================
// Types
// ============================================================================

export interface LanguageRules {
  keywords?: string[]
  types?: string[]
  builtins?: string[]
  patterns: Array<{ pattern: RegExp; className: string }>
}

// ============================================================================
// Shared Patterns
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

// ============================================================================
// Keyword Lists
// ============================================================================

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

// ============================================================================
// Core Language Definitions
// ============================================================================

export const languages: Record<string, LanguageRules> = {
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

  // Merge in additional languages (Rust, Go, SQL)
  ...extraLanguages,
}

// ============================================================================
// Aliases
// ============================================================================

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
