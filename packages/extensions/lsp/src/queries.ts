/**
 * LSP query helpers — high-level operations over the LSP client.
 */

import type { LSPClient } from './client.js'
import { pathToUri, uriToPath } from './server-manager.js'
import type { LSPDiagnostic, LSPHoverResult, LSPLocation, LSPPosition } from './types.js'

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
