/**
 * LSP tools — 9 tools for Language Server Protocol operations.
 */

import type { AnyTool } from '@ava/core-v2/tools'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'
import type { LSPClient } from './client.js'
import {
  formatCodeActions,
  formatDiagnostics,
  formatDocumentSymbols,
  formatHover,
  formatLocations,
  formatWorkspaceEdit,
  formatWorkspaceSymbols,
} from './queries.js'
import { pathToUri } from './server-manager.js'
import type { LSPDiagnostic, SupportedLanguage } from './types.js'

const languageSchema = z
  .enum(['typescript', 'python', 'rust', 'go', 'java'])
  .describe('Language: typescript, python, rust, go, java')

const positionSchema = z.object({
  file: z.string().describe('File path'),
  line: z.number().describe('Line number (1-based)'),
  column: z.number().describe('Column number (1-based)'),
  language: languageSchema,
})

interface LSPToolDeps {
  getServerManager: () => LSPServerManager | null
  getDiagnosticsStore: () => Map<string, LSPDiagnostic[]>
}

interface LSPServerManager {
  getClient(language: SupportedLanguage): LSPClient | null
}

function getClient(
  deps: LSPToolDeps,
  language: SupportedLanguage
): { client: LSPClient } | { error: string } {
  const mgr = deps.getServerManager()
  if (!mgr) return { error: 'LSP not initialized. Open a session first.' }
  const client = mgr.getClient(language)
  if (!client) return { error: `No LSP server running for ${language}` }
  return { client }
}

export function createDiagnosticsTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_diagnostics',
    description: 'Get LSP diagnostics (errors, warnings) for a file.',
    schema: z.object({
      file: z.string().describe('File path to get diagnostics for'),
    }),
    async execute(input) {
      const diags = deps.getDiagnosticsStore().get(input.file) ?? []
      return { success: true, output: formatDiagnostics(diags) }
    },
  })
}

export function createHoverTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_hover',
    description: 'Get hover information (type info, docs) for a symbol at a position.',
    schema: positionSchema,
    async execute(input) {
      const result = getClient(deps, input.language)
      if ('error' in result) return { success: false, output: result.error }
      try {
        const uri = pathToUri(input.file)
        const hover = await result.client.hover(uri, {
          line: input.line - 1,
          character: input.column - 1,
        })
        return { success: true, output: formatHover(hover) }
      } catch (err) {
        return { success: false, output: `LSP hover failed: ${err}` }
      }
    },
  })
}

export function createDefinitionTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_definition',
    description: 'Go to definition of a symbol at a position.',
    schema: positionSchema,
    async execute(input) {
      const result = getClient(deps, input.language)
      if ('error' in result) return { success: false, output: result.error }
      try {
        const uri = pathToUri(input.file)
        const locations = await result.client.definition(uri, {
          line: input.line - 1,
          character: input.column - 1,
        })
        return { success: true, output: formatLocations(locations) }
      } catch (err) {
        return { success: false, output: `LSP definition failed: ${err}` }
      }
    },
  })
}

export function createReferencesTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_references',
    description: 'Find all references to a symbol at a position across the project.',
    schema: positionSchema.extend({
      includeDeclaration: z
        .boolean()
        .optional()
        .describe('Include the declaration in results (default: true)'),
    }),
    async execute(input) {
      const result = getClient(deps, input.language)
      if ('error' in result) return { success: false, output: result.error }
      try {
        const uri = pathToUri(input.file)
        const locations = await result.client.references(
          uri,
          { line: input.line - 1, character: input.column - 1 },
          input.includeDeclaration ?? true
        )
        return { success: true, output: formatLocations(locations) }
      } catch (err) {
        return { success: false, output: `LSP references failed: ${err}` }
      }
    },
  })
}

export function createDocumentSymbolsTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_document_symbols',
    description:
      'Get all symbols (functions, classes, variables, etc.) in a file as a structured tree.',
    schema: z.object({
      file: z.string().describe('File path'),
      language: languageSchema,
    }),
    async execute(input) {
      const result = getClient(deps, input.language)
      if ('error' in result) return { success: false, output: result.error }
      try {
        const uri = pathToUri(input.file)
        const symbols = await result.client.documentSymbols(uri)
        return { success: true, output: formatDocumentSymbols(symbols) }
      } catch (err) {
        return { success: false, output: `LSP document symbols failed: ${err}` }
      }
    },
  })
}

export function createWorkspaceSymbolsTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_workspace_symbols',
    description: 'Search for symbols across the entire workspace by name or pattern.',
    schema: z.object({
      query: z.string().describe('Symbol name or search pattern'),
      language: languageSchema,
    }),
    async execute(input) {
      const result = getClient(deps, input.language)
      if ('error' in result) return { success: false, output: result.error }
      try {
        const symbols = await result.client.workspaceSymbols(input.query)
        return { success: true, output: formatWorkspaceSymbols(symbols) }
      } catch (err) {
        return { success: false, output: `LSP workspace symbols failed: ${err}` }
      }
    },
  })
}

export function createCodeActionsTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_code_actions',
    description: 'Get available code actions (quick fixes, refactorings) for a range in a file.',
    schema: z.object({
      file: z.string().describe('File path'),
      startLine: z.number().describe('Start line (1-based)'),
      startColumn: z.number().describe('Start column (1-based)'),
      endLine: z.number().describe('End line (1-based)'),
      endColumn: z.number().describe('End column (1-based)'),
      language: languageSchema,
    }),
    async execute(input) {
      const result = getClient(deps, input.language)
      if ('error' in result) return { success: false, output: result.error }
      try {
        const uri = pathToUri(input.file)
        const range = {
          start: { line: input.startLine - 1, character: input.startColumn - 1 },
          end: { line: input.endLine - 1, character: input.endColumn - 1 },
        }
        const actions = await result.client.codeActions(uri, range)
        return { success: true, output: formatCodeActions(actions) }
      } catch (err) {
        return { success: false, output: `LSP code actions failed: ${err}` }
      }
    },
  })
}

export function createRenameTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_rename',
    description: 'Rename a symbol at a position across the project using the LSP server.',
    schema: positionSchema.extend({
      newName: z.string().describe('The new name for the symbol'),
    }),
    async execute(input) {
      const result = getClient(deps, input.language)
      if ('error' in result) return { success: false, output: result.error }
      try {
        const uri = pathToUri(input.file)
        const edit = await result.client.rename(
          uri,
          { line: input.line - 1, character: input.column - 1 },
          input.newName
        )
        if (!edit) return { success: true, output: 'Rename returned no edits.' }
        return { success: true, output: formatWorkspaceEdit(edit) }
      } catch (err) {
        return { success: false, output: `LSP rename failed: ${err}` }
      }
    },
  })
}

export function createCompletionsTool(deps: LSPToolDeps): AnyTool {
  return defineTool({
    name: 'lsp_completions',
    description: 'Get code completions at a position in a file.',
    schema: positionSchema,
    async execute(input) {
      const result = getClient(deps, input.language)
      if ('error' in result) return { success: false, output: result.error }
      try {
        const uri = pathToUri(input.file)
        const items = await result.client.completion(uri, {
          line: input.line - 1,
          character: input.column - 1,
        })
        if (items.length === 0) return { success: true, output: 'No completions available.' }
        const lines = items.slice(0, 50).map((item) => {
          const detail = item.detail ? ` — ${item.detail}` : ''
          return `${item.label}${detail}`
        })
        if (items.length > 50) {
          lines.push(`... and ${items.length - 50} more`)
        }
        return { success: true, output: lines.join('\n') }
      } catch (err) {
        return { success: false, output: `LSP completions failed: ${err}` }
      }
    },
  })
}

export function createAllLspTools(deps: LSPToolDeps): AnyTool[] {
  return [
    createDiagnosticsTool(deps),
    createHoverTool(deps),
    createDefinitionTool(deps),
    createReferencesTool(deps),
    createDocumentSymbolsTool(deps),
    createWorkspaceSymbolsTool(deps),
    createCodeActionsTool(deps),
    createRenameTool(deps),
    createCompletionsTool(deps),
  ]
}
