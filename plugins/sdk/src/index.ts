/**
 * @ava-ai/plugin — SDK for building AVA plugins.
 *
 * Plugins communicate with AVA via JSON-RPC 2.0 over stdio using
 * Content-Length framing (identical to LSP/MCP wire format).
 *
 * Usage:
 *   import { createPlugin } from "@ava-ai/plugin";
 *   createPlugin({ "tool.before": async (ctx, params) => { ... } });
 */

import * as fs from 'fs'

// -- Public types --

export interface PluginContext {
  project: { directory: string; name: string }
  config: Record<string, unknown>
  tools: string[]
}

export type HookHandler = (
  ctx: PluginContext,
  params: Record<string, unknown>
) => Promise<Record<string, unknown> | void>

export interface PluginHooks {
  auth?: HookHandler
  'auth.refresh'?: HookHandler
  'request.headers'?: HookHandler
  'tool.before'?: HookHandler
  'tool.after'?: HookHandler
  'agent.before'?: HookHandler
  'agent.after'?: HookHandler
  'session.start'?: HookHandler
  'session.end'?: HookHandler
  config?: HookHandler
  event?: HookHandler
  'shell.env'?: HookHandler
}

// -- Internal types --

interface JsonRpcRequest {
  jsonrpc: '2.0'
  id?: number | string
  method: string
  params?: unknown
}

interface JsonRpcResponse {
  jsonrpc: '2.0'
  id: number | string | null
  result?: unknown
  error?: { code: number; message: string; data?: unknown }
}

// -- Wire format helpers --

function writeMessage(msg: JsonRpcResponse): void {
  const payload = JSON.stringify(msg)
  const frame = `Content-Length: ${Buffer.byteLength(payload)}\r\n\r\n${payload}`
  // Use synchronous write to stdout fd so data is flushed before process.exit
  fs.writeSync(1, frame)
}

function sendResult(id: number | string | null, result: unknown): void {
  writeMessage({ jsonrpc: '2.0', id, result: result ?? null })
}

function sendError(
  id: number | string | null,
  code: number,
  message: string,
  data?: unknown
): void {
  writeMessage({ jsonrpc: '2.0', id, error: { code, message, data } })
}

// -- Public API --

/**
 * Create and start an AVA plugin.
 *
 * Reads JSON-RPC messages from stdin, dispatches to user-defined hook
 * handlers, and writes responses to stdout.
 *
 * @param hooks - Map of hook names to async handler functions.
 */
export function createPlugin(hooks: PluginHooks): void {
  const context: PluginContext = {
    project: { directory: '', name: '' },
    config: {},
    tools: [],
  }

  let buffer = Buffer.alloc(0)

  // Queue for sequential async message processing
  let processing = Promise.resolve()

  async function handleMessage(msg: JsonRpcRequest): Promise<void> {
    const { id, method, params } = msg

    // -- initialize --
    if (method === 'initialize') {
      const p = (params ?? {}) as Record<string, unknown>
      if (p.project && typeof p.project === 'object') {
        const proj = p.project as Record<string, string>
        context.project = {
          directory: proj.directory ?? '',
          name: proj.name ?? '',
        }
      }
      if (p.config && typeof p.config === 'object') {
        context.config = p.config as Record<string, unknown>
      }
      if (Array.isArray(p.tools)) {
        context.tools = p.tools as string[]
      }

      sendResult(id ?? null, { hooks: Object.keys(hooks) })
      return
    }

    // -- shutdown --
    if (method === 'shutdown') {
      process.exit(0)
    }

    // -- hook/* dispatch --
    if (method && method.startsWith('hook/')) {
      const hookName = method.slice(5) as keyof PluginHooks
      const handler = hooks[hookName]

      if (!handler) {
        if (id !== undefined && id !== null) {
          sendError(id, -32601, `no handler for hook '${hookName}'`)
        }
        return
      }

      try {
        const result = await handler(context, (params ?? {}) as Record<string, unknown>)
        if (id !== undefined && id !== null) {
          sendResult(id, result ?? null)
        }
      } catch (err: unknown) {
        const message = err instanceof Error ? err.message : String(err)
        if (id !== undefined && id !== null) {
          sendError(id, -32000, message)
        } else {
          process.stderr.write(`[plugin] hook error: ${message}\n`)
        }
      }
      return
    }

    // -- unknown method --
    if (id !== undefined && id !== null) {
      sendError(id, -32601, `unknown method '${method}'`)
    }
  }

  function drainBuffer(): void {
    while (true) {
      const headerEnd = buffer.indexOf('\r\n\r\n')
      if (headerEnd === -1) break

      const headerText = buffer.subarray(0, headerEnd).toString('utf-8')
      const match = headerText.match(/content-length:\s*(\d+)/i)
      if (!match) {
        buffer = buffer.subarray(headerEnd + 4)
        continue
      }

      const contentLength = parseInt(match[1], 10)
      const bodyStart = headerEnd + 4
      const messageEnd = bodyStart + contentLength

      if (buffer.length < messageEnd) break

      const bodyText = buffer.subarray(bodyStart, messageEnd).toString('utf-8')
      buffer = buffer.subarray(messageEnd)

      try {
        const msg = JSON.parse(bodyText) as JsonRpcRequest
        // Chain messages sequentially so async handlers complete in order
        processing = processing.then(() => handleMessage(msg))
      } catch {
        // Ignore unparseable messages
      }
    }
  }

  process.stdin.on('data', (chunk: Buffer) => {
    buffer = Buffer.concat([buffer, chunk])
    drainBuffer()
  })

  process.stdin.resume()
}
