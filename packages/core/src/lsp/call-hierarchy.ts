/**
 * Call Hierarchy
 * Extract call hierarchy information from code
 *
 * Provides:
 * - Incoming calls (who calls this function)
 * - Outgoing calls (what does this function call)
 *
 * Uses static analysis via language-specific tools
 */

import { spawn } from 'node:child_process'
import { extname } from 'node:path'

// ============================================================================
// Types
// ============================================================================

/**
 * A symbol in the call hierarchy
 */
export interface CallHierarchyItem {
  /** Symbol name */
  name: string
  /** Symbol kind (function, method, class, etc.) */
  kind: SymbolKind
  /** File path */
  uri: string
  /** Range of the symbol definition */
  range: Range
  /** Selection range (usually the symbol name) */
  selectionRange: Range
  /** Optional detail (e.g., signature) */
  detail?: string
}

/**
 * An incoming call (who calls this function)
 */
export interface CallHierarchyIncomingCall {
  /** The item that makes the call */
  from: CallHierarchyItem
  /** Ranges within the caller where calls are made */
  fromRanges: Range[]
}

/**
 * An outgoing call (what does this function call)
 */
export interface CallHierarchyOutgoingCall {
  /** The item that is called */
  to: CallHierarchyItem
  /** Ranges within the current item where calls are made */
  fromRanges: Range[]
}

/**
 * Result of preparing call hierarchy
 */
export interface PrepareCallHierarchyResult {
  /** Items at the position */
  items: CallHierarchyItem[]
  /** Time taken */
  durationMs: number
  /** Error if any */
  error?: string
}

/**
 * Result of getting incoming/outgoing calls
 */
export interface CallHierarchyCallsResult {
  /** The calls (incoming or outgoing) */
  calls: CallHierarchyIncomingCall[] | CallHierarchyOutgoingCall[]
  /** Time taken */
  durationMs: number
  /** Error if any */
  error?: string
}

/**
 * Position in a text document
 */
export interface Range {
  start: Position
  end: Position
}

export interface Position {
  line: number
  character: number
}

/**
 * Symbol kinds (subset of LSP SymbolKind)
 */
export enum SymbolKind {
  File = 1,
  Module = 2,
  Namespace = 3,
  Package = 4,
  Class = 5,
  Method = 6,
  Property = 7,
  Field = 8,
  Constructor = 9,
  Enum = 10,
  Interface = 11,
  Function = 12,
  Variable = 13,
  Constant = 14,
}

// ============================================================================
// TypeScript Call Hierarchy (using grep-based analysis)
// ============================================================================

/**
 * Find function calls in TypeScript/JavaScript files
 * Uses simple pattern matching for common patterns
 */
export async function getTypeScriptCallHierarchy(
  filePath: string,
  functionName: string,
  cwd: string
): Promise<PrepareCallHierarchyResult> {
  const startTime = Date.now()

  try {
    // Use grep to find function definition
    const definition = await findFunctionDefinition(filePath, functionName, cwd)

    if (!definition) {
      return {
        items: [],
        durationMs: Date.now() - startTime,
        error: `Function "${functionName}" not found in ${filePath}`,
      }
    }

    return {
      items: [definition],
      durationMs: Date.now() - startTime,
    }
  } catch (err) {
    return {
      items: [],
      durationMs: Date.now() - startTime,
      error: err instanceof Error ? err.message : String(err),
    }
  }
}

/**
 * Find incoming calls to a function
 */
export async function getIncomingCalls(
  functionName: string,
  cwd: string,
  extensions: string[] = ['.ts', '.tsx', '.js', '.jsx']
): Promise<CallHierarchyCallsResult> {
  const startTime = Date.now()

  try {
    // Search for function calls using ripgrep
    const calls = await searchFunctionCalls(functionName, cwd, extensions)

    return {
      calls: calls.map((call) => ({
        from: call.caller,
        fromRanges: [call.range],
      })),
      durationMs: Date.now() - startTime,
    }
  } catch (err) {
    return {
      calls: [],
      durationMs: Date.now() - startTime,
      error: err instanceof Error ? err.message : String(err),
    }
  }
}

/**
 * Find outgoing calls from a function
 * Analyzes function body for function calls
 */
export async function getOutgoingCalls(
  filePath: string,
  functionName: string,
  cwd: string
): Promise<CallHierarchyCallsResult> {
  const startTime = Date.now()

  try {
    const calls = await analyzeFunctionCalls(filePath, functionName, cwd)

    return {
      calls: calls.map((call) => ({
        to: call.callee,
        fromRanges: [call.range],
      })),
      durationMs: Date.now() - startTime,
    }
  } catch (err) {
    return {
      calls: [],
      durationMs: Date.now() - startTime,
      error: err instanceof Error ? err.message : String(err),
    }
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

interface FunctionCall {
  caller: CallHierarchyItem
  range: Range
}

interface OutgoingCall {
  callee: CallHierarchyItem
  range: Range
}

/**
 * Find function definition using grep
 */
async function findFunctionDefinition(
  filePath: string,
  functionName: string,
  cwd: string
): Promise<CallHierarchyItem | null> {
  return new Promise((resolve) => {
    // Pattern to match function declarations
    const pattern = `(function\\s+${functionName}|const\\s+${functionName}\\s*=|${functionName}\\s*[:=]\\s*(async\\s+)?function|${functionName}\\s*\\()`

    const proc = spawn('grep', ['-n', '-E', pattern, filePath], {
      cwd,
      shell: true,
    })

    let stdout = ''

    proc.stdout.on('data', (data) => {
      stdout += data.toString()
    })

    proc.on('close', () => {
      const lines = stdout.trim().split('\n').filter(Boolean)
      if (lines.length === 0) {
        resolve(null)
        return
      }

      // Parse first match
      const firstLine = lines[0]
      const colonIndex = firstLine.indexOf(':')
      if (colonIndex === -1) {
        resolve(null)
        return
      }

      const lineNum = parseInt(firstLine.slice(0, colonIndex), 10) - 1

      resolve({
        name: functionName,
        kind: SymbolKind.Function,
        uri: filePath,
        range: {
          start: { line: lineNum, character: 0 },
          end: { line: lineNum, character: 100 },
        },
        selectionRange: {
          start: { line: lineNum, character: 0 },
          end: { line: lineNum, character: functionName.length },
        },
      })
    })

    proc.on('error', () => {
      resolve(null)
    })
  })
}

/**
 * Search for function calls across the codebase
 */
async function searchFunctionCalls(
  functionName: string,
  cwd: string,
  extensions: string[]
): Promise<FunctionCall[]> {
  return new Promise((resolve) => {
    // Build include patterns for extensions
    const includePatterns = extensions.map((ext) => `--include=*${ext}`).join(' ')

    // Pattern to match function calls (not definitions)
    const pattern = `${functionName}\\s*\\(`

    const proc = spawn('grep', ['-r', '-n', '-E', pattern, ...includePatterns.split(' '), '.'], {
      cwd,
      shell: true,
    })

    let stdout = ''

    proc.stdout.on('data', (data) => {
      stdout += data.toString()
    })

    proc.on('close', () => {
      const calls: FunctionCall[] = []
      const lines = stdout.trim().split('\n').filter(Boolean)

      for (const line of lines) {
        // Format: ./path/to/file.ts:123:content
        const match = line.match(/^\.?\/?(.*?):(\d+):(.*)$/)
        if (!match) continue

        const [, file, lineStr, content] = match
        const lineNum = parseInt(lineStr, 10) - 1

        // Skip definition lines (where function is declared)
        if (
          content.includes(`function ${functionName}`) ||
          content.includes(`const ${functionName} =`) ||
          content.includes(`${functionName}:`) ||
          content.match(new RegExp(`^\\s*(export\\s+)?(async\\s+)?function\\s+${functionName}`))
        ) {
          continue
        }

        // Try to find the caller function name
        const callerName = extractCallerName(content, lineNum)

        calls.push({
          caller: {
            name: callerName || 'unknown',
            kind: SymbolKind.Function,
            uri: file,
            range: {
              start: { line: lineNum, character: 0 },
              end: { line: lineNum, character: content.length },
            },
            selectionRange: {
              start: { line: lineNum, character: content.indexOf(functionName) },
              end: {
                line: lineNum,
                character: content.indexOf(functionName) + functionName.length,
              },
            },
          },
          range: {
            start: { line: lineNum, character: content.indexOf(functionName) },
            end: { line: lineNum, character: content.indexOf(functionName) + functionName.length },
          },
        })
      }

      resolve(calls)
    })

    proc.on('error', () => {
      resolve([])
    })
  })
}

/**
 * Analyze function body for outgoing calls
 */
async function analyzeFunctionCalls(
  filePath: string,
  functionName: string,
  cwd: string
): Promise<OutgoingCall[]> {
  return new Promise((resolve) => {
    // This is a simplified version - a full implementation would parse the AST
    const proc = spawn('cat', [filePath], { cwd })

    let content = ''

    proc.stdout.on('data', (data) => {
      content += data.toString()
    })

    proc.on('close', () => {
      const calls: OutgoingCall[] = []
      const lines = content.split('\n')

      // Find the function and extract its body
      let inFunction = false
      let braceCount = 0

      for (let i = 0; i < lines.length; i++) {
        const line = lines[i]

        // Check if this is the function definition
        if (
          !inFunction &&
          (line.includes(`function ${functionName}`) ||
            line.includes(`const ${functionName}`) ||
            line.match(new RegExp(`${functionName}\\s*[=:]`)))
        ) {
          inFunction = true
          braceCount = (line.match(/\{/g) || []).length - (line.match(/\}/g) || []).length
          continue
        }

        if (inFunction) {
          braceCount += (line.match(/\{/g) || []).length
          braceCount -= (line.match(/\}/g) || []).length

          // Find function calls in this line
          const callPattern = /(\w+)\s*\(/g
          const matches = line.matchAll(callPattern)
          for (const match of matches) {
            const calledFn = match[1]
            // Skip common keywords and the function itself
            if (
              ['if', 'for', 'while', 'switch', 'catch', 'function', functionName].includes(calledFn)
            ) {
              continue
            }

            calls.push({
              callee: {
                name: calledFn,
                kind: SymbolKind.Function,
                uri: filePath,
                range: {
                  start: { line: i, character: match.index },
                  end: { line: i, character: match.index + calledFn.length },
                },
                selectionRange: {
                  start: { line: i, character: match.index },
                  end: { line: i, character: match.index + calledFn.length },
                },
              },
              range: {
                start: { line: i, character: match.index },
                end: { line: i, character: match.index + calledFn.length },
              },
            })
          }

          // End of function
          if (braceCount <= 0) {
            break
          }
        }
      }

      resolve(calls)
    })

    proc.on('error', () => {
      resolve([])
    })
  })
}

/**
 * Extract the name of the function that contains this line
 */
function extractCallerName(content: string, _lineNum: number): string | null {
  // Simple heuristic: look for function-like patterns before the call
  const funcMatch = content.match(/(?:function\s+)?(\w+)\s*(?:=\s*(?:async\s+)?function|\()/)
  if (funcMatch?.[1]) {
    return funcMatch[1]
  }
  return null
}

// ============================================================================
// Language Detection
// ============================================================================

/**
 * Get file extensions for call hierarchy based on file type
 */
export function getCallHierarchyExtensions(filePath: string): string[] {
  const ext = extname(filePath).toLowerCase()

  switch (ext) {
    case '.ts':
    case '.tsx':
    case '.js':
    case '.jsx':
      return ['.ts', '.tsx', '.js', '.jsx']
    case '.py':
      return ['.py']
    case '.go':
      return ['.go']
    case '.rs':
      return ['.rs']
    case '.java':
      return ['.java']
    default:
      return [ext]
  }
}
