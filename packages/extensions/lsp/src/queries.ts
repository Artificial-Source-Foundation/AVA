/**
 * LSP query helpers — high-level operations over the LSP client.
 */

import type { LSPClient } from './client.js'
import { pathToUri, uriToPath } from './server-manager.js'
import type {
  LSPCodeAction,
  LSPDiagnostic,
  LSPDocumentSymbol,
  LSPHoverResult,
  LSPLocation,
  LSPPosition,
  LSPWorkspaceSymbol,
} from './types.js'

/** Format hover result to plain text. */
export function formatHover(hover: LSPHoverResult | null): string {
  if (!hover) return 'No hover information available.'

  const { contents } = hover
  if (typeof contents === 'string') return contents
  if ('value' in contents) return contents.value
  if (Array.isArray(contents)) {
    return contents.map((c) => (typeof c === 'string' ? c : c.value)).join('\n')
  }
  return String(contents)
}

/** Format locations as "file:line:col" strings. */
export function formatLocations(locations: LSPLocation[]): string {
  if (locations.length === 0) return 'No results found.'
  return locations
    .map((loc) => {
      const path = uriToPath(loc.uri)
      const line = loc.range.start.line + 1
      const col = loc.range.start.character + 1
      return `${path}:${line}:${col}`
    })
    .join('\n')
}

/** Format diagnostics as human-readable report. */
export function formatDiagnostics(diagnostics: LSPDiagnostic[]): string {
  if (diagnostics.length === 0) return 'No diagnostics.'

  const byFile = new Map<string, LSPDiagnostic[]>()
  for (const d of diagnostics) {
    const list = byFile.get(d.file) ?? []
    list.push(d)
    byFile.set(d.file, list)
  }

  const lines: string[] = []
  for (const [file, diags] of byFile) {
    lines.push(`${file}:`)
    for (const d of diags) {
      const source = d.source ? ` (${d.source})` : ''
      lines.push(`  ${d.line}:${d.column} [${d.severity}]${source} ${d.message}`)
    }
  }
  return lines.join('\n')
}

/** Symbol kind number → human-readable name. */
const SYMBOL_KIND_NAMES: Record<number, string> = {
  1: 'File',
  2: 'Module',
  3: 'Namespace',
  4: 'Package',
  5: 'Class',
  6: 'Method',
  7: 'Property',
  8: 'Field',
  9: 'Constructor',
  10: 'Enum',
  11: 'Interface',
  12: 'Function',
  13: 'Variable',
  14: 'Constant',
  15: 'String',
  16: 'Number',
  17: 'Boolean',
  18: 'Array',
  19: 'Object',
  20: 'Key',
  21: 'Null',
  22: 'EnumMember',
  23: 'Struct',
  24: 'Event',
  25: 'Operator',
  26: 'TypeParameter',
}

function symbolKindName(kind: number): string {
  return SYMBOL_KIND_NAMES[kind] ?? `Kind(${kind})`
}

/** Format document symbols as an indented tree. */
export function formatDocumentSymbols(symbols: LSPDocumentSymbol[]): string {
  if (symbols.length === 0) return 'No symbols found.'

  const lines: string[] = []
  function walk(syms: LSPDocumentSymbol[], indent: number): void {
    for (const sym of syms) {
      const prefix = '  '.repeat(indent)
      const kind = symbolKindName(sym.kind)
      const detail = sym.detail ? ` — ${sym.detail}` : ''
      const line = sym.range.start.line + 1
      lines.push(`${prefix}${kind} ${sym.name}${detail} (line ${line})`)
      if (sym.children && sym.children.length > 0) {
        walk(sym.children, indent + 1)
      }
    }
  }
  walk(symbols, 0)
  return lines.join('\n')
}

/** Format workspace symbols as "file:line kind name" strings. */
export function formatWorkspaceSymbols(symbols: LSPWorkspaceSymbol[]): string {
  if (symbols.length === 0) return 'No symbols found.'
  return symbols
    .map((sym) => {
      const path = uriToPath(sym.location.uri)
      const line = sym.location.range.start.line + 1
      const kind = symbolKindName(sym.kind)
      return `${path}:${line} [${kind}] ${sym.name}`
    })
    .join('\n')
}

/** Format code actions as a numbered list. */
export function formatCodeActions(actions: LSPCodeAction[]): string {
  if (actions.length === 0) return 'No code actions available.'
  return actions
    .map((action, i) => {
      const kind = action.kind ? ` (${action.kind})` : ''
      const hasEdit = action.edit ? ' [has edit]' : ''
      const hasCmd = action.command ? ` [cmd: ${action.command.command}]` : ''
      return `${i + 1}. ${action.title}${kind}${hasEdit}${hasCmd}`
    })
    .join('\n')
}

/** Format a workspace edit as a summary of changes per file. */
export function formatWorkspaceEdit(
  edit: {
    changes: Record<string, Array<{ range: { start: { line: number } }; newText: string }>>
  } | null
): string {
  if (!edit || !edit.changes) return 'No changes.'
  const lines: string[] = []
  for (const [uri, edits] of Object.entries(edit.changes)) {
    const path = uriToPath(uri)
    lines.push(`${path}: ${edits.length} edit(s)`)
    for (const e of edits) {
      const line = e.range.start.line + 1
      const preview = e.newText.length > 60 ? `${e.newText.slice(0, 60)}...` : e.newText
      lines.push(`  line ${line}: ${preview.replace(/\n/g, '\\n')}`)
    }
  }
  return lines.join('\n')
}

/** Get hover info for a position in a file. */
export async function queryHover(
  client: LSPClient,
  filePath: string,
  position: LSPPosition
): Promise<string> {
  const uri = pathToUri(filePath)
  const hover = await client.hover(uri, position)
  return formatHover(hover)
}

/** Get definition locations for a position. */
export async function queryDefinition(
  client: LSPClient,
  filePath: string,
  position: LSPPosition
): Promise<string> {
  const uri = pathToUri(filePath)
  const locations = await client.definition(uri, position)
  return formatLocations(locations)
}

/** Get references for a position. */
export async function queryReferences(
  client: LSPClient,
  filePath: string,
  position: LSPPosition
): Promise<string> {
  const uri = pathToUri(filePath)
  const locations = await client.references(uri, position)
  return formatLocations(locations)
}
