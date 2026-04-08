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

import * as fs from 'node:fs'

// -- Public types --

export interface PluginContext {
  project: { directory: string; name: string }
  config: Record<string, unknown>
  tools: string[]
}

export interface PluginCommandSpec {
  name: string
  description?: string
}

export interface PluginRouteSpec {
  path: string
  method: string
  description?: string
}

export interface PluginEventSpec {
  name: string
  description?: string
}

export interface PluginMountSpec {
  id: string
  location: string
  label: string
  description?: string
}

export interface PluginAppCapabilities {
  commands?: PluginCommandSpec[]
  routes?: PluginRouteSpec[]
  events?: PluginEventSpec[]
  mounts?: PluginMountSpec[]
}

export interface PluginAppEvent {
  event: string
  payload?: unknown
}

export interface PluginAppResponse {
  result?: unknown
  emittedEvents?: PluginAppEvent[]
}

export type HookHandler = (
  ctx: PluginContext,
  params: Record<string, unknown>
) => Promise<Record<string, unknown> | undefined>

export type AppCommandHandler = (ctx: PluginContext, payload: unknown) => Promise<PluginAppResponse>

export type AppRouteHandler = (
  ctx: PluginContext,
  request: {
    method: string
    path: string
    query: Record<string, unknown>
    body?: unknown
  }
) => Promise<PluginAppResponse>

export interface PluginAppHandlers {
  capabilities?: PluginAppCapabilities
  commands?: Record<string, AppCommandHandler>
  routes?: Record<string, AppRouteHandler>
}

// -- Typed params/result shapes for the 4 new hooks --

/**
 * Params for the `tool.definition` hook.
 * Plugins receive the full list of tool definitions and may return a modified list.
 */
export interface ToolDefinitionParams {
  tools: Array<{ name: string; description: string; parameters: unknown }>
}

export interface ToolDefinitionResult {
  tools: Array<{ name: string; description: string; parameters: unknown }>
}

/**
 * Params for the `chat.params` hook.
 * Plugins may override any field; returned object is merged into the call config.
 */
export interface ChatParamsPayload {
  model: string
  max_tokens?: number
  thinking_level?: string
  thinking_budget_tokens?: number | null
  [key: string]: unknown
}

/**
 * Params for the `permission.ask` hook.
 * Return `{action: "allow"}` to approve, `{action: "deny", reason: "..."}` to reject.
 * Return nothing (or `null`) to defer to the interactive approval flow.
 */
export interface PermissionAskParams {
  tool: string
  arguments: Record<string, unknown>
  risk_level: string
  reason: string
}

export interface PermissionAskResult {
  action: 'allow' | 'deny'
  reason?: string
}

/**
 * Params for the `chat.system` hook.
 * Return `{inject: "..."}` to append text to the system prompt.
 */
export interface ChatSystemParams {
  model: string
  provider: string
}

export interface ChatSystemResult {
  inject: string
}

/**
 * Params for the `chat.messages.transform` hook.
 * Plugins may return a modified message array under `"messages"`.
 * Each message must have a `role` field or it will be discarded.
 * Plugins are chained — each receives the output of the previous one.
 */
export interface ChatMessagesTransformParams {
  messages: Array<{ role: string; content: string }>
}

export interface ChatMessagesTransformResult {
  messages: Array<{ role: string; content: string }>
}

/**
 * Params for the `session.compacting` hook.
 * Return `{context: [...], prompt: "..."}` to inject extra context strings
 * or supply a custom compaction prompt (first non-empty `prompt` wins).
 */
export interface SessionCompactingParams {
  session_id: string
  message_count: number
  token_count: number
}

export interface SessionCompactingResult {
  /** Extra context strings to inject before compaction. */
  context?: string[]
  /** Custom compaction prompt (overrides default). */
  prompt?: string
}

/**
 * Params for the `chat.message` notification hook.
 * Fired on each new user message before the agent processes it.
 * No response expected (notification only).
 */
export interface ChatMessageParams {
  session_id: string
  message: { role: 'user'; content: string }
}

/**
 * Params for the `text.complete` notification hook.
 * Fired when a text response finishes streaming.
 * No response expected (notification only).
 */
export interface TextCompleteParams {
  session_id: string
  content: string
  token_count: number
}

/**
 * Params for the `command.execute.before` hook.
 * Return `{block: true, reason: "..."}` to prevent the command from running.
 * Return nothing (or `{block: false}`) to allow execution.
 */
export interface CommandExecuteBeforeParams {
  command: string
  arguments: string
}

export interface CommandExecuteBeforeResult {
  block: boolean
  reason?: string
}

export interface PluginHooks {
  auth?: HookHandler
  'auth.refresh'?: HookHandler
  'request.headers'?: HookHandler
  'tool.before'?: HookHandler
  'tool.after'?: HookHandler
  /**
   * Modify tool definitions (description/schema) before they are sent to the LLM.
   * Return `{ tools: [...] }` with the (possibly modified) list.
   */
  'tool.definition'?: (
    ctx: PluginContext,
    params: ToolDefinitionParams
  ) => Promise<ToolDefinitionResult | undefined>
  /**
   * Modify LLM call parameters (max_tokens, thinking_budget_tokens, etc.) per call.
   * Return an object with any fields to override — it is merged into the call config.
   */
  'chat.params'?: (
    ctx: PluginContext,
    params: ChatParamsPayload
  ) => Promise<Partial<ChatParamsPayload> | undefined>
  /**
   * Programmatically approve or deny a permission request.
   * Return `{action: "allow"}` or `{action: "deny", reason: "..."}`.
   * Return nothing to defer to the interactive approval flow.
   */
  'permission.ask'?: (
    ctx: PluginContext,
    params: PermissionAskParams
  ) => Promise<PermissionAskResult | undefined>
  /**
   * Inject text into the system prompt before each agent run.
   * Return `{inject: "<text>"}` to append the text.
   */
  'chat.system'?: (
    ctx: PluginContext,
    params: ChatSystemParams
  ) => Promise<ChatSystemResult | undefined>
  'agent.before'?: HookHandler
  'agent.after'?: HookHandler
  'session.start'?: HookHandler
  'session.end'?: HookHandler
  config?: HookHandler
  event?: HookHandler
  'shell.env'?: HookHandler
  /**
   * Transform conversation messages before they are sent to the LLM.
   * Return `{ messages: [...] }` with the (possibly modified) message list.
   * Plugins are chained — each receives the output of the previous one.
   * Messages lacking a `role` field are silently discarded.
   */
  'chat.messages.transform'?: (
    ctx: PluginContext,
    params: ChatMessagesTransformParams
  ) => Promise<ChatMessagesTransformResult | undefined>
  /**
   * Fires before context compaction starts.
   * Return `{context: [...], prompt: "..."}` to inject extra context strings
   * or supply a custom compaction prompt.
   */
  'session.compacting'?: (
    ctx: PluginContext,
    params: SessionCompactingParams
  ) => Promise<SessionCompactingResult | undefined>
  /**
   * Fires on each new user message before the agent processes it.
   * Notification — no response expected.
   */
  'chat.message'?: (ctx: PluginContext, params: ChatMessageParams) => Promise<void>
  /**
   * Fires when a text response finishes streaming.
   * Notification — no response expected.
   */
  'text.complete'?: (ctx: PluginContext, params: TextCompleteParams) => Promise<void>
  /**
   * Fires before a slash command executes.
   * Return `{block: true, reason: "..."}` to prevent execution.
   */
  'command.execute.before'?: (
    ctx: PluginContext,
    params: CommandExecuteBeforeParams
  ) => Promise<CommandExecuteBeforeResult | undefined>
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
export function createPlugin(hooks: PluginHooks, app: PluginAppHandlers = {}): void {
  const context: PluginContext = {
    project: { directory: '', name: '' },
    config: {},
    tools: [],
  }

  let buffer = Buffer.alloc(0)
  let shouldShutdown = false
  const routeHandlers = Object.fromEntries(
    Object.entries(app.routes ?? {}).map(([key, handler]) => [key.toUpperCase(), handler])
  ) as Record<string, AppRouteHandler>

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

      sendResult(id ?? null, {
        hooks: Object.keys(hooks),
        app: app.capabilities ?? {},
      })
      return
    }

    // -- shutdown --
    if (method === 'shutdown') {
      shouldShutdown = true
      return
    }

    if (method === 'app.command') {
      const p = (params ?? {}) as Record<string, unknown>
      const command = typeof p.command === 'string' ? p.command : ''
      const handler = command ? app.commands?.[command] : undefined
      if (!handler) {
        sendError(id ?? null, -32601, `unknown app command '${command}'`)
        return
      }

      try {
        const result = await handler(context, p.payload)
        sendResult(id ?? null, result ?? { result: null, emittedEvents: [] })
      } catch (err: unknown) {
        const message = err instanceof Error ? err.message : String(err)
        sendError(id ?? null, -32000, message)
      }
      return
    }

    if (method === 'app.route') {
      const p = (params ?? {}) as Record<string, unknown>
      const routeKey = `${String(p.method ?? '').toUpperCase()} ${String(p.path ?? '')}`
      const handler = routeHandlers[routeKey]
      if (!handler) {
        sendError(id ?? null, -32601, `unknown app route '${routeKey}'`)
        return
      }

      try {
        const result = await handler(context, {
          method: String(p.method ?? ''),
          path: String(p.path ?? ''),
          query: (p.query as Record<string, unknown>) ?? {},
          body: p.body,
        })
        sendResult(id ?? null, result ?? { result: null, emittedEvents: [] })
      } catch (err: unknown) {
        const message = err instanceof Error ? err.message : String(err)
        sendError(id ?? null, -32000, message)
      }
      return
    }

    // -- hook/* dispatch --
    if (method?.startsWith('hook/')) {
      const hookName = method.slice(5) as keyof PluginHooks
      // Use a generic function type so all hook handler shapes (HookHandler and
      // the strongly-typed new hooks) are callable in a uniform way.
      const handler = hooks[hookName] as
        | ((ctx: PluginContext, params: unknown) => Promise<unknown>)
        | undefined

      if (!handler) {
        if (id !== undefined && id !== null) {
          sendError(id, -32601, `no handler for hook '${hookName}'`)
        }
        return
      }

      try {
        const result = await handler(context, params ?? {})
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
        processing = processing
          .then(() => handleMessage(msg))
          .then(() => {
            if (shouldShutdown) process.exit(0)
          })
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
