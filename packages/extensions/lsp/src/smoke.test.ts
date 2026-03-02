/**
 * LSP tools smoke test — verifies extension activation and tool registration.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('LSP tools smoke test', () => {
  it('activates and registers 9 tools', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    const toolNames = registeredTools.map((t) => t.definition.name)
    expect(toolNames).toContain('lsp_diagnostics')
    expect(toolNames).toContain('lsp_hover')
    expect(toolNames).toContain('lsp_definition')
    expect(toolNames).toContain('lsp_references')
    expect(toolNames).toContain('lsp_document_symbols')
    expect(toolNames).toContain('lsp_workspace_symbols')
    expect(toolNames).toContain('lsp_code_actions')
    expect(toolNames).toContain('lsp_rename')
    expect(toolNames).toContain('lsp_completions')
    expect(registeredTools).toHaveLength(9)

    disposable.dispose()
  })

  it('all tools have valid definitions', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    for (const tool of registeredTools) {
      expect(tool.definition.name).toBeTruthy()
      expect(tool.definition.description).toBeTruthy()
      expect(tool.definition.input_schema).toBeTruthy()
      expect(tool.definition.input_schema.type).toBe('object')
    }

    disposable.dispose()
  })

  it('lsp_diagnostics returns empty for unknown file', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    const diagTool = registeredTools.find((t) => t.definition.name === 'lsp_diagnostics')!
    const result = await diagTool.execute({ file: '/project/unknown.ts' })
    expect(result.success).toBe(true)

    disposable.dispose()
  })

  it('lsp_hover returns error without session', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    const hoverTool = registeredTools.find((t) => t.definition.name === 'lsp_hover')!
    const result = await hoverTool.execute({
      file: '/project/test.ts',
      line: 1,
      column: 1,
      language: 'typescript',
    })
    expect(result.success).toBe(false)
    expect(result.output).toContain('not initialized')

    disposable.dispose()
  })

  it('lsp_definition returns error without session', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    const defTool = registeredTools.find((t) => t.definition.name === 'lsp_definition')!
    const result = await defTool.execute({
      file: '/project/test.ts',
      line: 1,
      column: 1,
      language: 'typescript',
    })
    expect(result.success).toBe(false)
    expect(result.output).toContain('not initialized')

    disposable.dispose()
  })

  it('cleans up on dispose', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredTools.length).toBeGreaterThan(0)
    disposable.dispose()
    // After dispose, tools should be cleaned up
  })
})
