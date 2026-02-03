/**
 * Import Parsing
 * Parse import statements from source code
 *
 * Supports:
 * - ES modules (import/export)
 * - CommonJS (require)
 * - TypeScript path aliases
 */

import type { ExportInfo, ImportInfo, Language } from './types.js'

// ============================================================================
// Import Parsing
// ============================================================================

/**
 * Parse imports from source code
 *
 * @param content - Source code content
 * @param language - Programming language
 * @returns Array of parsed imports
 */
export function parseImports(content: string, language: Language): ImportInfo[] {
  switch (language) {
    case 'typescript':
    case 'javascript':
      return parseTypeScriptImports(content)
    case 'python':
      return parsePythonImports(content)
    case 'go':
      return parseGoImports(content)
    default:
      return []
  }
}

/**
 * Parse exports from source code
 *
 * @param content - Source code content
 * @param language - Programming language
 * @returns Array of parsed exports
 */
export function parseExports(content: string, language: Language): ExportInfo[] {
  switch (language) {
    case 'typescript':
    case 'javascript':
      return parseTypeScriptExports(content)
    default:
      return []
  }
}

// ============================================================================
// TypeScript/JavaScript Import Parsing
// ============================================================================

/**
 * Parse TypeScript/JavaScript imports
 */
function parseTypeScriptImports(content: string): ImportInfo[] {
  const imports: ImportInfo[] = []
  const lines = content.split('\n')

  // Patterns for different import styles
  const patterns = {
    // import { foo, bar } from 'module'
    // import { foo as f, bar } from 'module'
    namedImport: /^import\s+(?:type\s+)?{([^}]+)}\s+from\s+['"]([^'"]+)['"]/,

    // import * as name from 'module'
    namespaceImport: /^import\s+\*\s+as\s+(\w+)\s+from\s+['"]([^'"]+)['"]/,

    // import name from 'module'
    defaultImport: /^import\s+(\w+)\s+from\s+['"]([^'"]+)['"]/,

    // import name, { foo } from 'module'
    mixedImport: /^import\s+(\w+)\s*,\s*{([^}]+)}\s+from\s+['"]([^'"]+)['"]/,

    // import 'module' (side effect)
    sideEffectImport: /^import\s+['"]([^'"]+)['"]/,

    // const x = require('module')
    commonJS: /(?:const|let|var)\s+(?:{([^}]+)}|\w+)\s*=\s*require\s*\(\s*['"]([^'"]+)['"]\s*\)/,

    // Dynamic import: import('module')
    dynamicImport: /import\s*\(\s*['"]([^'"]+)['"]\s*\)/,

    // Re-export: export { foo } from 'module'
    reExport: /^export\s+(?:type\s+)?{([^}]+)}\s+from\s+['"]([^'"]+)['"]/,

    // Re-export all: export * from 'module'
    reExportAll: /^export\s+\*\s+from\s+['"]([^'"]+)['"]/,
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim()
    const lineNum = i + 1

    // Named import
    let match = line.match(patterns.namedImport)
    if (match) {
      const specifiers = parseNamedSpecifiers(match[1])
      const isType = line.startsWith('import type')
      imports.push({
        source: match[2],
        specifiers,
        isType,
        isNamespace: false,
        line: lineNum,
      })
      continue
    }

    // Namespace import
    match = line.match(patterns.namespaceImport)
    if (match) {
      imports.push({
        source: match[2],
        specifiers: [],
        defaultImport: match[1],
        isType: false,
        isNamespace: true,
        line: lineNum,
      })
      continue
    }

    // Mixed import (default + named)
    match = line.match(patterns.mixedImport)
    if (match) {
      const specifiers = parseNamedSpecifiers(match[2])
      imports.push({
        source: match[3],
        specifiers,
        defaultImport: match[1],
        isType: false,
        isNamespace: false,
        line: lineNum,
      })
      continue
    }

    // Default import
    match = line.match(patterns.defaultImport)
    if (match) {
      imports.push({
        source: match[2],
        specifiers: [],
        defaultImport: match[1],
        isType: false,
        isNamespace: false,
        line: lineNum,
      })
      continue
    }

    // Side effect import
    match = line.match(patterns.sideEffectImport)
    if (match) {
      imports.push({
        source: match[1],
        specifiers: [],
        isType: false,
        isNamespace: false,
        line: lineNum,
      })
      continue
    }

    // CommonJS require
    match = line.match(patterns.commonJS)
    if (match) {
      const specifiers = match[1] ? parseNamedSpecifiers(match[1]) : []
      imports.push({
        source: match[2],
        specifiers,
        isType: false,
        isNamespace: false,
        line: lineNum,
      })
      continue
    }

    // Re-export
    match = line.match(patterns.reExport)
    if (match) {
      const specifiers = parseNamedSpecifiers(match[1])
      imports.push({
        source: match[2],
        specifiers,
        isType: line.includes('export type'),
        isNamespace: false,
        line: lineNum,
      })
      continue
    }

    // Re-export all
    match = line.match(patterns.reExportAll)
    if (match) {
      imports.push({
        source: match[1],
        specifiers: [],
        isType: false,
        isNamespace: true,
        line: lineNum,
      })
    }
  }

  return imports
}

/**
 * Parse named specifiers from import { foo, bar as b } format
 */
function parseNamedSpecifiers(specifiersStr: string): string[] {
  return specifiersStr
    .split(',')
    .map((s) => {
      // Handle 'foo as bar' -> 'foo'
      const parts = s.trim().split(/\s+as\s+/)
      return parts[0].trim()
    })
    .filter((s) => s.length > 0)
}

// ============================================================================
// TypeScript/JavaScript Export Parsing
// ============================================================================

/**
 * Parse TypeScript/JavaScript exports
 */
function parseTypeScriptExports(content: string): ExportInfo[] {
  const exports: ExportInfo[] = []
  const lines = content.split('\n')

  const patterns = {
    // export default
    defaultExport: /^export\s+default\s+(?:class|function|const|let|var)?\s*(\w+)?/,

    // export { foo, bar }
    namedExport: /^export\s+(?:type\s+)?{([^}]+)}/,

    // export const/let/var/function/class/interface/type/enum
    declarationExport:
      /^export\s+(?:type\s+)?(?:const|let|var|function|async function|class|interface|type|enum)\s+(\w+)/,

    // export * from
    reExportAll: /^export\s+\*\s+from\s+['"]([^'"]+)['"]/,

    // export { foo } from
    reExportNamed: /^export\s+(?:type\s+)?{([^}]+)}\s+from\s+['"]([^'"]+)['"]/,
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim()
    const lineNum = i + 1

    // Default export
    let match = line.match(patterns.defaultExport)
    if (match) {
      exports.push({
        name: match[1] || 'default',
        isDefault: true,
        isType: false,
        line: lineNum,
      })
      continue
    }

    // Named export with braces
    match = line.match(patterns.namedExport)
    if (match && !line.includes('from')) {
      const names = parseNamedSpecifiers(match[1])
      const isType = line.includes('export type')
      for (const name of names) {
        exports.push({
          name,
          isDefault: false,
          isType,
          line: lineNum,
        })
      }
      continue
    }

    // Declaration export
    match = line.match(patterns.declarationExport)
    if (match) {
      const isType =
        line.includes('interface ') || line.includes('type ') || line.includes('export type ')
      exports.push({
        name: match[1],
        isDefault: false,
        isType,
        line: lineNum,
      })
      continue
    }

    // Re-export all
    match = line.match(patterns.reExportAll)
    if (match) {
      exports.push({
        name: '*',
        isDefault: false,
        isType: false,
        reExportFrom: match[1],
        line: lineNum,
      })
      continue
    }

    // Re-export named
    match = line.match(patterns.reExportNamed)
    if (match) {
      const names = parseNamedSpecifiers(match[1])
      const isType = line.includes('export type')
      for (const name of names) {
        exports.push({
          name,
          isDefault: false,
          isType,
          reExportFrom: match[2],
          line: lineNum,
        })
      }
    }
  }

  return exports
}

// ============================================================================
// Python Import Parsing
// ============================================================================

/**
 * Parse Python imports
 */
function parsePythonImports(content: string): ImportInfo[] {
  const imports: ImportInfo[] = []
  const lines = content.split('\n')

  const patterns = {
    // from module import foo, bar
    fromImport: /^from\s+([\w.]+)\s+import\s+(.+)/,
    // import module
    simpleImport: /^import\s+([\w.]+)(?:\s+as\s+\w+)?/,
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim()
    const lineNum = i + 1

    let match = line.match(patterns.fromImport)
    if (match) {
      const specifiers = match[2]
        .split(',')
        .map((s) => s.trim().split(/\s+as\s+/)[0])
        .filter((s) => s && s !== '*')
      imports.push({
        source: match[1],
        specifiers,
        isType: false,
        isNamespace: match[2].trim() === '*',
        line: lineNum,
      })
      continue
    }

    match = line.match(patterns.simpleImport)
    if (match) {
      imports.push({
        source: match[1],
        specifiers: [],
        isType: false,
        isNamespace: true,
        line: lineNum,
      })
    }
  }

  return imports
}

// ============================================================================
// Go Import Parsing
// ============================================================================

/**
 * Parse Go imports
 */
function parseGoImports(content: string): ImportInfo[] {
  const imports: ImportInfo[] = []

  // Single import
  const singlePattern = /import\s+"([^"]+)"/g
  for (const match of content.matchAll(singlePattern)) {
    imports.push({
      source: match[1],
      specifiers: [],
      isType: false,
      isNamespace: true,
      line: 0, // Line tracking for Go would need more work
    })
  }

  // Import block
  const blockPattern = /import\s+\(([\s\S]*?)\)/g
  for (const match of content.matchAll(blockPattern)) {
    const block = match[1]
    const importLines = block.split('\n')
    for (const line of importLines) {
      const pathMatch = line.match(/"([^"]+)"/)
      if (pathMatch) {
        imports.push({
          source: pathMatch[1],
          specifiers: [],
          isType: false,
          isNamespace: true,
          line: 0,
        })
      }
    }
  }

  return imports
}

// ============================================================================
// Import Resolution
// ============================================================================

/**
 * Resolve an import path to an actual file path
 *
 * @param importPath - The import path (e.g., './utils', '@/lib/foo')
 * @param fromFile - The file containing the import
 * @param options - Resolution options
 * @returns Resolved file path or null if not found
 */
export function resolveImportPath(
  importPath: string,
  fromFile: string,
  options: {
    rootPath: string
    paths?: Record<string, string[]>
    extensions?: string[]
  }
): string | null {
  const { rootPath, paths = {}, extensions = ['.ts', '.tsx', '.js', '.jsx', '.json'] } = options

  // Skip external packages
  if (!importPath.startsWith('.') && !importPath.startsWith('@/') && !importPath.startsWith('~/')) {
    return null
  }

  // Get directory of importing file
  const fromDir = fromFile.substring(0, fromFile.lastIndexOf('/'))

  let resolvedBase: string

  // Handle path aliases
  for (const [alias, targets] of Object.entries(paths)) {
    const aliasPattern = alias.replace('*', '')
    if (importPath.startsWith(aliasPattern)) {
      const rest = importPath.slice(aliasPattern.length)
      for (const target of targets) {
        const targetBase = target.replace('*', '')
        resolvedBase = `${rootPath}/${targetBase}${rest}`
        const resolved = tryResolve(resolvedBase, extensions)
        if (resolved) return resolved
      }
    }
  }

  // Handle relative imports
  if (importPath.startsWith('.')) {
    resolvedBase = normalizePath(`${fromDir}/${importPath}`)
    return tryResolve(resolvedBase, extensions)
  }

  return null
}

/**
 * Try to resolve a path with different extensions
 */
function tryResolve(basePath: string, extensions: string[]): string | null {
  // Try exact path first
  // Note: In a real implementation, we'd check if the file exists
  // For now, we just append extensions

  // If it already has an extension, return as-is
  if (extensions.some((ext) => basePath.endsWith(ext))) {
    return basePath
  }

  // Try with each extension
  for (const ext of extensions) {
    const withExt = basePath + ext
    // In a real implementation: if (await fs.exists(withExt)) return withExt
    return withExt // For now, just return the first attempt
  }

  // Try as directory with index
  for (const ext of extensions) {
    const indexPath = `${basePath}/index${ext}`
    // In a real implementation: if (await fs.exists(indexPath)) return indexPath
    return indexPath
  }

  return null
}

/**
 * Normalize a file path (resolve .. and .)
 */
function normalizePath(path: string): string {
  const parts = path.split('/')
  const result: string[] = []

  for (const part of parts) {
    if (part === '..') {
      result.pop()
    } else if (part !== '.' && part !== '') {
      result.push(part)
    }
  }

  return result.join('/')
}
