/**
 * LSP tools tests — verifies tool definitions, schemas, and error handling.
 */

import { describe, expect, it, vi } from 'vitest'
import type { LSPClient } from './client.js'
import {
  createAllLspTools,
  createCodeActionsTool,
  createCompletionsTool,
  createDefinitionTool,
  createDiagnosticsTool,
  createDocumentSymbolsTool,
  createHoverTool,
  createReferencesTool,
  createRenameTool,
  createWorkspaceSymbolsTool,
} from './tools.js'
import type { LSPDiagnostic, SupportedLanguage } from './types.js'

// ─── Test Helpers ─────────────────────────────────────────────────────────

function createMockClient(): LSPClient {
  return {
    hover: vi.fn().mockResolvedValue(null),
    definition: vi.fn().mockResolvedValue([]),
    references: vi.fn().mockResolvedValue([]),
    documentSymbols: vi.fn().mockResolvedValue([]),
    workspaceSymbols: vi.fn().mockResolvedValue([]),
    codeActions: vi.fn().mockResolvedValue([]),
    rename: vi.fn().mockResolvedValue(null),
    completion: vi.fn().mockResolvedValue([]),
  } as unknown as LSPClient
}

interface MockServerManager {
  getClient(language: SupportedLanguage): LSPClient | null
}

function createDeps(options?: {
  client?: LSPClient | null
  manager?: MockServerManager | null
  diagnostics?: Map<string, LSPDiagnostic[]>
}) {
  const client = options?.client ?? null
  const diagnostics = options?.diagnostics ?? new Map<string, LSPDiagnostic[]>()
  const manager: MockServerManager | null =
    options?.manager !== undefined ? options.manager : client ? { getClient: () => client } : null
  return {
    getServerManager: () => manager,
    getDiagnosticsStore: () => diagnostics,
  }
}

// ─── Tests ────────────────────────────────────────────────────────────────

describe('LSP tools', () => {
  describe('createAllLspTools', () => {
    it('creates 9 tools', () => {
      const tools = createAllLspTools(createDeps())
      expect(tools).toHaveLength(9)
    })

    it('all tools have valid definitions', () => {
      const tools = createAllLspTools(createDeps())
      for (const tool of tools) {
        expect(tool.definition.name).toBeTruthy()
        expect(tool.definition.name).toMatch(/^lsp_/)
        expect(tool.definition.description).toBeTruthy()
        expect(tool.definition.input_schema).toBeTruthy()
        expect(tool.definition.input_schema.type).toBe('object')
      }
    })

    it('all tool names are unique', () => {
      const tools = createAllLspTools(createDeps())
      const names = tools.map((t) => t.definition.name)
      expect(new Set(names).size).toBe(names.length)
    })
  })

  describe('lsp_diagnostics', () => {
    it('returns empty diagnostics for unknown file', async () => {
      const tool = createDiagnosticsTool(createDeps())
      const result = await tool.execute({ file: '/unknown.ts' }, {} as never)
      expect(result.success).toBe(true)
      expect(result.output).toBe('No diagnostics.')
    })

    it('returns stored diagnostics', async () => {
      const diagnostics = new Map<string, LSPDiagnostic[]>()
      diagnostics.set('/project/main.ts', [
        { file: '/project/main.ts', line: 5, column: 3, severity: 'error', message: 'Type error' },
      ])
      const tool = createDiagnosticsTool(createDeps({ diagnostics }))
      const result = await tool.execute({ file: '/project/main.ts' }, {} as never)
      expect(result.success).toBe(true)
      expect(result.output).toContain('Type error')
      expect(result.output).toContain('5:3')
    })
  })

  describe('lsp_hover', () => {
    it('returns error without server manager', async () => {
      const tool = createHoverTool(createDeps())
      const result = await tool.execute(
        { file: '/test.ts', line: 1, column: 1, language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(result.success).toBe(false)
      expect(result.output).toContain('not initialized')
    })

    it('returns error when no server for language', async () => {
      const manager = { getClient: () => null }
      const tool = createHoverTool(createDeps({ manager }))
      const result = await tool.execute(
        { file: '/test.py', line: 1, column: 1, language: 'python' as SupportedLanguage },
        {} as never
      )
      expect(result.success).toBe(false)
      expect(result.output).toContain('No LSP server running for python')
    })
  })

  describe('lsp_definition', () => {
    it('calls client.definition with 0-based position', async () => {
      const client = createMockClient()
      const tool = createDefinitionTool(createDeps({ client }))
      await tool.execute(
        { file: '/test.ts', line: 10, column: 5, language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(client.definition).toHaveBeenCalledWith('file:///test.ts', { line: 9, character: 4 })
    })
  })

  describe('lsp_references', () => {
    it('calls client.references with includeDeclaration', async () => {
      const client = createMockClient()
      const tool = createReferencesTool(createDeps({ client }))
      await tool.execute(
        {
          file: '/test.ts',
          line: 3,
          column: 7,
          language: 'typescript' as SupportedLanguage,
          includeDeclaration: false,
        },
        {} as never
      )
      expect(client.references).toHaveBeenCalledWith(
        'file:///test.ts',
        { line: 2, character: 6 },
        false
      )
    })

    it('defaults includeDeclaration to true', async () => {
      const client = createMockClient()
      const tool = createReferencesTool(createDeps({ client }))
      await tool.execute(
        { file: '/test.ts', line: 1, column: 1, language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(client.references).toHaveBeenCalledWith(
        'file:///test.ts',
        { line: 0, character: 0 },
        true
      )
    })
  })

  describe('lsp_document_symbols', () => {
    it('calls client.documentSymbols and formats result', async () => {
      const client = createMockClient()
      ;(client.documentSymbols as ReturnType<typeof vi.fn>).mockResolvedValue([
        {
          name: 'MyClass',
          kind: 5,
          range: { start: { line: 0, character: 0 }, end: { line: 10, character: 0 } },
          children: [
            {
              name: 'myMethod',
              kind: 6,
              range: { start: { line: 2, character: 2 }, end: { line: 5, character: 3 } },
            },
          ],
        },
      ])
      const tool = createDocumentSymbolsTool(createDeps({ client }))
      const result = await tool.execute(
        { file: '/test.ts', language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('Class MyClass')
      expect(result.output).toContain('Method myMethod')
    })

    it('returns error without server', async () => {
      const tool = createDocumentSymbolsTool(createDeps())
      const result = await tool.execute(
        { file: '/test.ts', language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(result.success).toBe(false)
    })
  })

  describe('lsp_workspace_symbols', () => {
    it('calls client.workspaceSymbols', async () => {
      const client = createMockClient()
      ;(client.workspaceSymbols as ReturnType<typeof vi.fn>).mockResolvedValue([
        {
          name: 'handleRequest',
          kind: 12,
          location: {
            uri: 'file:///project/handler.ts',
            range: { start: { line: 5, character: 0 }, end: { line: 20, character: 1 } },
          },
        },
      ])
      const tool = createWorkspaceSymbolsTool(createDeps({ client }))
      const result = await tool.execute(
        { query: 'handle', language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('handleRequest')
      expect(result.output).toContain('Function')
    })
  })

  describe('lsp_code_actions', () => {
    it('calls client.codeActions with correct range', async () => {
      const client = createMockClient()
      const tool = createCodeActionsTool(createDeps({ client }))
      await tool.execute(
        {
          file: '/test.ts',
          startLine: 5,
          startColumn: 1,
          endLine: 5,
          endColumn: 10,
          language: 'typescript' as SupportedLanguage,
        },
        {} as never
      )
      expect(client.codeActions).toHaveBeenCalledWith('file:///test.ts', {
        start: { line: 4, character: 0 },
        end: { line: 4, character: 9 },
      })
    })

    it('formats code actions with titles and kinds', async () => {
      const client = createMockClient()
      ;(client.codeActions as ReturnType<typeof vi.fn>).mockResolvedValue([
        { title: 'Add missing import', kind: 'quickfix' },
        { title: 'Extract to function', kind: 'refactor.extract' },
      ])
      const tool = createCodeActionsTool(createDeps({ client }))
      const result = await tool.execute(
        {
          file: '/test.ts',
          startLine: 1,
          startColumn: 1,
          endLine: 1,
          endColumn: 5,
          language: 'typescript' as SupportedLanguage,
        },
        {} as never
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('Add missing import')
      expect(result.output).toContain('quickfix')
      expect(result.output).toContain('Extract to function')
    })
  })

  describe('lsp_rename', () => {
    it('calls client.rename with newName', async () => {
      const client = createMockClient()
      const tool = createRenameTool(createDeps({ client }))
      await tool.execute(
        {
          file: '/test.ts',
          line: 3,
          column: 10,
          language: 'typescript' as SupportedLanguage,
          newName: 'betterName',
        },
        {} as never
      )
      expect(client.rename).toHaveBeenCalledWith(
        'file:///test.ts',
        { line: 2, character: 9 },
        'betterName'
      )
    })

    it('returns "no edits" when rename returns null', async () => {
      const client = createMockClient()
      ;(client.rename as ReturnType<typeof vi.fn>).mockResolvedValue(null)
      const tool = createRenameTool(createDeps({ client }))
      const result = await tool.execute(
        {
          file: '/test.ts',
          line: 1,
          column: 1,
          language: 'typescript' as SupportedLanguage,
          newName: 'x',
        },
        {} as never
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('no edits')
    })

    it('formats workspace edit summary', async () => {
      const client = createMockClient()
      ;(client.rename as ReturnType<typeof vi.fn>).mockResolvedValue({
        changes: {
          'file:///project/main.ts': [
            {
              range: { start: { line: 2, character: 4 }, end: { line: 2, character: 10 } },
              newText: 'newVar',
            },
          ],
          'file:///project/util.ts': [
            {
              range: { start: { line: 8, character: 0 }, end: { line: 8, character: 6 } },
              newText: 'newVar',
            },
          ],
        },
      })
      const tool = createRenameTool(createDeps({ client }))
      const result = await tool.execute(
        {
          file: '/test.ts',
          line: 1,
          column: 1,
          language: 'typescript' as SupportedLanguage,
          newName: 'newVar',
        },
        {} as never
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('/project/main.ts: 1 edit(s)')
      expect(result.output).toContain('/project/util.ts: 1 edit(s)')
    })
  })

  describe('lsp_completions', () => {
    it('returns formatted completions', async () => {
      const client = createMockClient()
      ;(client.completion as ReturnType<typeof vi.fn>).mockResolvedValue([
        { label: 'forEach', detail: '(method) Array.forEach' },
        { label: 'filter', detail: '(method) Array.filter' },
      ])
      const tool = createCompletionsTool(createDeps({ client }))
      const result = await tool.execute(
        { file: '/test.ts', line: 5, column: 10, language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('forEach')
      expect(result.output).toContain('filter')
    })

    it('returns "no completions" for empty results', async () => {
      const client = createMockClient()
      const tool = createCompletionsTool(createDeps({ client }))
      const result = await tool.execute(
        { file: '/test.ts', line: 1, column: 1, language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('No completions available')
    })

    it('handles LSP errors gracefully', async () => {
      const client = createMockClient()
      ;(client.completion as ReturnType<typeof vi.fn>).mockRejectedValue(
        new Error('Server disconnected')
      )
      const tool = createCompletionsTool(createDeps({ client }))
      const result = await tool.execute(
        { file: '/test.ts', line: 1, column: 1, language: 'typescript' as SupportedLanguage },
        {} as never
      )
      expect(result.success).toBe(false)
      expect(result.output).toContain('Server disconnected')
    })
  })
})
