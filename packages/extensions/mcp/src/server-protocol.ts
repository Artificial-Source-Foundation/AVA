import { executeTool, getToolDefinitions } from '@ava/core-v2/tools'

import type { JSONRPCNotification, JSONRPCRequest, JSONRPCResponse } from './transport.js'

interface ToolExecutionContext {
  sessionId: string
  workingDirectory: string
  signal: AbortSignal
}

function toSuccessResponse(id: number, result: unknown): JSONRPCResponse {
  return { jsonrpc: '2.0', id, result }
}

function toErrorResponse(id: number, code: number, message: string): JSONRPCResponse {
  return { jsonrpc: '2.0', id, error: { code, message } }
}

function normalizeToolDefinitions(): Array<{
  name: string
  description: string
  inputSchema: Record<string, unknown>
}> {
  return getToolDefinitions().map((tool) => ({
    name: tool.name,
    description: tool.description,
    inputSchema: tool.input_schema,
  }))
}

export async function handleMCPServerRequest(
  request: JSONRPCRequest,
  ctx: ToolExecutionContext
): Promise<JSONRPCResponse> {
  if (request.method === 'initialize') {
    return toSuccessResponse(request.id, {
      protocolVersion: '2024-11-05',
      capabilities: {
        tools: {},
      },
      serverInfo: {
        name: 'ava-mcp-server',
        version: '1.0.0',
      },
    })
  }

  if (request.method === 'tools/list') {
    return toSuccessResponse(request.id, { tools: normalizeToolDefinitions() })
  }

  if (request.method === 'tools/call') {
    const params = request.params ?? {}
    const name = params.name
    const args = params.arguments
    if (typeof name !== 'string' || typeof args !== 'object' || args === null) {
      return toErrorResponse(request.id, -32602, 'Invalid tools/call params')
    }

    const result = await executeTool(name, args as Record<string, unknown>, {
      sessionId: ctx.sessionId,
      workingDirectory: ctx.workingDirectory,
      signal: ctx.signal,
    })

    return toSuccessResponse(request.id, {
      content: [{ type: 'text', text: result.output || result.error || '' }],
      isError: !result.success,
    })
  }

  return toErrorResponse(request.id, -32601, `Method not found: ${request.method}`)
}

export function createToolsListChangedNotification(): JSONRPCNotification {
  return {
    jsonrpc: '2.0',
    method: 'notifications/tools/list_changed',
  }
}
