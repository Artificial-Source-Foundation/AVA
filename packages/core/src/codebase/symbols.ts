/**
 * Symbol Extraction
 * Extract functions, classes, interfaces, and types from source code
 *
 * Uses regex-based parsing for simplicity and portability.
 * No tree-sitter dependency required.
 */

import type { CodeSymbol, Language, SymbolType } from './types.js'

// ============================================================================
// Symbol Extraction
// ============================================================================

/**
 * Extract symbols from source code content
 *
 * @param content - Source code content
 * @param language - Programming language
 * @returns Array of extracted symbols
 */
export function extractSymbols(content: string, language: Language): CodeSymbol[] {
  switch (language) {
    case 'typescript':
    case 'javascript':
      return extractTypeScriptSymbols(content)
    case 'python':
      return extractPythonSymbols(content)
    case 'go':
      return extractGoSymbols(content)
    case 'rust':
      return extractRustSymbols(content)
    default:
      return []
  }
}

// ============================================================================
// TypeScript/JavaScript Extraction
// ============================================================================

/**
 * Extract symbols from TypeScript/JavaScript code
 */
function extractTypeScriptSymbols(content: string): CodeSymbol[] {
  const symbols: CodeSymbol[] = []
  const lines = content.split('\n')

  // Track current class/namespace for nested symbols
  let currentParent: string | undefined

  // Patterns for TypeScript/JavaScript
  const patterns = {
    // export function name(
    // export async function name(
    // function name(
    // async function name(
    function: /^(\s*)(?:export\s+)?(?:async\s+)?function\s+(\w+)\s*(?:<[^>]*>)?\s*\(/,

    // export const name = (...) =>
    // export const name = async (...) =>
    // const name = (...) =>
    arrowFunction:
      /^(\s*)(?:export\s+)?(?:const|let)\s+(\w+)\s*(?::\s*[^=]+)?\s*=\s*(?:async\s*)?\(?[^)]*\)?\s*=>/,

    // export class Name
    // class Name
    class: /^(\s*)(?:export\s+)?(?:abstract\s+)?class\s+(\w+)/,

    // export interface Name
    // interface Name
    interface: /^(\s*)(?:export\s+)?interface\s+(\w+)/,

    // export type Name =
    // type Name =
    type: /^(\s*)(?:export\s+)?type\s+(\w+)\s*(?:<[^>]*>)?\s*=/,

    // export enum Name
    // enum Name
    enum: /^(\s*)(?:export\s+)?enum\s+(\w+)/,

    // export const NAME = (all caps, likely constant)
    constant: /^(\s*)(?:export\s+)?const\s+([A-Z][A-Z0-9_]+)\s*=/,

    // export namespace Name
    // namespace Name
    namespace: /^(\s*)(?:export\s+)?namespace\s+(\w+)/,

    // Method inside class: name(
    method: /^(\s+)(?:async\s+)?(\w+)\s*(?:<[^>]*>)?\s*\([^)]*\)\s*(?::\s*[^{]+)?\s*\{/,

    // Property: name:
    property: /^(\s+)(?:readonly\s+)?(\w+)\s*(?:\?)?:\s*[^;]+;/,
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    const lineNum = i + 1

    // Check for class/namespace end (simple brace counting won't work well,
    // so we just track the first level)
    if (currentParent && line.match(/^}\s*$/)) {
      currentParent = undefined
    }

    // Try each pattern
    for (const [type, pattern] of Object.entries(patterns)) {
      const match = line.match(pattern)
      if (!match) continue

      const indent = match[1]
      const name = match[2]

      // Skip if it's a keyword or looks invalid
      if (isReservedWord(name)) continue

      // Determine symbol type
      const symbolType = mapPatternToSymbolType(type)

      // Check if exported
      const exported = line.includes('export ')

      // Get signature for functions
      let signature: string | undefined
      if (symbolType === 'function' || symbolType === 'method') {
        signature = extractSignature(lines, i)
      }

      // Find end line (simplified: look for matching close brace or semicolon)
      const endLine = findEndLine(lines, i, indent.length)

      const symbol: CodeSymbol = {
        name,
        type: symbolType,
        line: lineNum,
        endLine,
        exported,
        signature,
        parent: type === 'method' || type === 'property' ? currentParent : undefined,
      }

      symbols.push(symbol)

      // Track parent for nested symbols
      if (type === 'class' || type === 'namespace') {
        currentParent = name
      }

      break // Only match one pattern per line
    }
  }

  return symbols
}

/**
 * Map pattern name to SymbolType
 */
function mapPatternToSymbolType(patternName: string): SymbolType {
  switch (patternName) {
    case 'function':
    case 'arrowFunction':
      return 'function'
    case 'class':
      return 'class'
    case 'interface':
      return 'interface'
    case 'type':
      return 'type'
    case 'enum':
      return 'enum'
    case 'constant':
      return 'constant'
    case 'namespace':
      return 'namespace'
    case 'method':
      return 'method'
    case 'property':
      return 'property'
    default:
      return 'variable'
  }
}

/**
 * Extract function signature
 */
function extractSignature(lines: string[], startLine: number): string {
  const line = lines[startLine]

  // Try to get the full signature from one line
  const funcMatch = line.match(
    /(?:async\s+)?(?:function\s+)?(\w+)\s*(<[^>]*>)?\s*\([^)]*\)(?:\s*:\s*[^{]+)?/
  )
  if (funcMatch) {
    return funcMatch[0].trim()
  }

  // Multi-line signature - collect until we see { or =>
  let signature = line.trim()
  for (let i = startLine + 1; i < Math.min(startLine + 5, lines.length); i++) {
    const nextLine = lines[i].trim()
    if (nextLine.includes('{') || nextLine.includes('=>')) {
      signature += ` ${nextLine.replace(/[{].*/, '').replace(/=>.*/, '').trim()}`
      break
    }
    signature += ` ${nextLine}`
  }

  return signature.slice(0, 200) // Limit length
}

/**
 * Find the end line of a symbol (simplified)
 */
function findEndLine(lines: string[], startLine: number, _startIndent: number): number {
  let braceCount = 0
  let foundOpen = false

  for (let i = startLine; i < lines.length; i++) {
    const line = lines[i]

    // Count braces
    for (const char of line) {
      if (char === '{') {
        braceCount++
        foundOpen = true
      } else if (char === '}') {
        braceCount--
      }
    }

    // Found matching close
    if (foundOpen && braceCount === 0) {
      return i + 1
    }

    // Single-line declarations (interfaces, types without body)
    if (!foundOpen && line.includes(';')) {
      return i + 1
    }
  }

  return startLine + 1
}

/**
 * Check if a name is a reserved word
 */
function isReservedWord(name: string): boolean {
  const reserved = new Set([
    'if',
    'else',
    'for',
    'while',
    'do',
    'switch',
    'case',
    'break',
    'continue',
    'return',
    'try',
    'catch',
    'finally',
    'throw',
    'new',
    'delete',
    'typeof',
    'instanceof',
    'void',
    'this',
    'super',
    'get',
    'set',
    'constructor',
  ])
  return reserved.has(name)
}

// ============================================================================
// Python Extraction
// ============================================================================

/**
 * Extract symbols from Python code
 */
function extractPythonSymbols(content: string): CodeSymbol[] {
  const symbols: CodeSymbol[] = []
  const lines = content.split('\n')

  const patterns = {
    function: /^(\s*)(?:async\s+)?def\s+(\w+)\s*\(/,
    class: /^(\s*)class\s+(\w+)/,
  }

  let currentClass: string | undefined
  let classIndent = 0

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    const lineNum = i + 1

    // Track class scope by indentation
    const currentIndent = line.match(/^(\s*)/)?.[1].length || 0
    if (currentClass && currentIndent <= classIndent && line.trim()) {
      currentClass = undefined
    }

    // Check patterns
    for (const [type, pattern] of Object.entries(patterns)) {
      const match = line.match(pattern)
      if (!match) continue

      const indent = match[1]
      const name = match[2]

      // Skip private methods starting with __
      if (name.startsWith('__') && name !== '__init__') continue

      const isMethod = type === 'function' && currentClass !== undefined
      const symbolType: SymbolType = isMethod ? 'method' : (type as SymbolType)

      symbols.push({
        name,
        type: symbolType,
        line: lineNum,
        endLine: findPythonEndLine(lines, i, indent.length),
        exported: !name.startsWith('_'),
        parent: isMethod ? currentClass : undefined,
      })

      if (type === 'class') {
        currentClass = name
        classIndent = indent.length
      }

      break
    }
  }

  return symbols
}

/**
 * Find end line for Python (by indentation)
 */
function findPythonEndLine(lines: string[], startLine: number, startIndent: number): number {
  for (let i = startLine + 1; i < lines.length; i++) {
    const line = lines[i]
    if (!line.trim()) continue // Skip empty lines

    const indent = line.match(/^(\s*)/)?.[1].length || 0
    if (indent <= startIndent) {
      return i
    }
  }
  return lines.length
}

// ============================================================================
// Go Extraction
// ============================================================================

/**
 * Extract symbols from Go code
 */
function extractGoSymbols(content: string): CodeSymbol[] {
  const symbols: CodeSymbol[] = []
  const lines = content.split('\n')

  const patterns = {
    function: /^func\s+(\w+)\s*\(/,
    method: /^func\s+\([^)]+\)\s+(\w+)\s*\(/,
    type: /^type\s+(\w+)\s+(?:struct|interface)/,
    constant: /^const\s+(\w+)\s*=/,
    variable: /^var\s+(\w+)\s+/,
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    const lineNum = i + 1

    for (const [type, pattern] of Object.entries(patterns)) {
      const match = line.match(pattern)
      if (!match) continue

      const name = match[1]
      const exported = name[0] === name[0].toUpperCase()

      symbols.push({
        name,
        type: type as SymbolType,
        line: lineNum,
        endLine: findEndLine(lines, i, 0),
        exported,
      })

      break
    }
  }

  return symbols
}

// ============================================================================
// Rust Extraction
// ============================================================================

/**
 * Extract symbols from Rust code
 */
function extractRustSymbols(content: string): CodeSymbol[] {
  const symbols: CodeSymbol[] = []
  const lines = content.split('\n')

  const patterns = {
    function: /^(\s*)(?:pub\s+)?(?:async\s+)?fn\s+(\w+)/,
    struct: /^(\s*)(?:pub\s+)?struct\s+(\w+)/,
    enum: /^(\s*)(?:pub\s+)?enum\s+(\w+)/,
    trait: /^(\s*)(?:pub\s+)?trait\s+(\w+)/,
    type: /^(\s*)(?:pub\s+)?type\s+(\w+)/,
    constant: /^(\s*)(?:pub\s+)?const\s+(\w+)/,
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    const lineNum = i + 1

    for (const [type, pattern] of Object.entries(patterns)) {
      const match = line.match(pattern)
      if (!match) continue

      const indent = match[1]
      const name = match[2]
      const exported = line.includes('pub ')

      // Map struct to class for consistency
      const symbolType: SymbolType = type === 'struct' ? 'class' : (type as SymbolType)

      symbols.push({
        name,
        type: symbolType,
        line: lineNum,
        endLine: findEndLine(lines, i, indent.length),
        exported,
      })

      break
    }
  }

  return symbols
}
