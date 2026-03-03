import { beforeEach, describe, expect, it, vi } from 'vitest'

const { executeToolMock, getToolDefinitionsMock } = vi.hoisted(() => ({
  executeToolMock: vi.fn(),
  getToolDefinitionsMock: vi.fn(),
}))

vi.mock('@ava/core-v2/tools', () => ({
  executeTool: executeToolMock,
  getToolDefinitions: getToolDefinitionsMock,
}))

import { createToolsListChangedNotification, handleMCPServerRequest } from './server-protocol.js'

function ctx() {
  return {
    sessionId: 's1',
    workingDirectory: '/repo',
    signal: AbortSignal.timeout(5000),
  }
}

describe('server-protocol', () => {
  beforeEach(() => {
    executeToolMock.mockReset()
    getToolDefinitionsMock.mockReset()
  })

  it('handles initialize request', async () => {
    const result = await handleMCPServerRequest(
      { jsonrpc: '2.0', id: 1, method: 'initialize' },
      ctx()
    )
    expect('result' in result).toBe(true)
    expect((result as { result: { protocolVersion: string } }).result.protocolVersion).toBe(
      '2024-11-05'
    )
  })

  it('handles tools/list using core tool definitions', async () => {
    getToolDefinitionsMock.mockReturnValue([
      { name: 'read_file', description: 'Read', input_schema: { type: 'object' } },
    ])

    const result = await handleMCPServerRequest(
      { jsonrpc: '2.0', id: 2, method: 'tools/list' },
      ctx()
    )
    expect((result as { result: { tools: Array<{ name: string }> } }).result.tools[0]?.name).toBe(
      'read_file'
    )
  })

  it('handles tools/call success', async () => {
    executeToolMock.mockResolvedValue({ success: true, output: 'ok' })
    const result = await handleMCPServerRequest(
      {
        jsonrpc: '2.0',
        id: 3,
        method: 'tools/call',
        params: { name: 'read_file', arguments: { path: '/tmp/a.txt' } },
      },
      ctx()
    )
    expect((result as { result: { isError: boolean } }).result.isError).toBe(false)
  })

  it('returns invalid params error for malformed tools/call', async () => {
    const result = await handleMCPServerRequest(
      {
        jsonrpc: '2.0',
        id: 4,
        method: 'tools/call',
        params: { name: 42, arguments: null },
      },
      ctx()
    )
    expect((result as { error: { code: number } }).error.code).toBe(-32602)
  })

  it('returns method not found for unsupported request', async () => {
    const result = await handleMCPServerRequest(
      { jsonrpc: '2.0', id: 5, method: 'unknown/method' },
      ctx()
    )
    expect((result as { error: { code: number } }).error.code).toBe(-32601)
  })

  it('creates tools/list_changed notification', () => {
    expect(createToolsListChangedNotification()).toEqual({
      jsonrpc: '2.0',
      method: 'notifications/tools/list_changed',
    })
  })
})
