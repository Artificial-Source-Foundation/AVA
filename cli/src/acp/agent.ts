/**
 * Estela ACP Agent
 *
 * Implements the Agent Client Protocol for integration with
 * Toad, Zed, and other ACP-compatible editors.
 *
 * Uses @estela/core ACP modules for:
 * - Session persistence (AcpSessionStore)
 * - Mode switching (AcpModeManager)
 * - MCP server forwarding (AcpMCPBridge)
 * - Error handling (AcpErrorHandler)
 *
 * Reference: https://agentclientprotocol.com/
 */

import {
  type Agent,
  AgentSideConnection,
  type AuthenticateRequest,
  type AuthenticateResponse,
  type CancelNotification,
  type InitializeRequest,
  type InitializeResponse,
  type NewSessionRequest,
  type NewSessionResponse,
  ndJsonStream,
  type PromptRequest,
  type PromptResponse,
  type SessionNotification,
} from '@agentclientprotocol/sdk'
import {
  AcpError,
  AcpErrorCode,
  type AcpErrorHandler,
  type AcpMCPBridge,
  type AcpMCPServerConfig,
  type AcpModeManager,
  // ACP modules
  type AcpSessionStore,
  type ChatMessage,
  createAcpErrorHandler,
  createAcpMCPBridge,
  createAcpModeManager,
  createAcpSessionStore,
  createClient,
  executeTool,
  getToolDefinitions,
  type ProviderConfig,
  resetToolCallCount,
  type ToolContext,
  type ToolUseBlock,
} from '@estela/core'

// ============================================================================
// Constants
// ============================================================================

const VERSION = '0.1.0'
const PROTOCOL_VERSION = 20250101
const DEFAULT_MODEL = 'claude-sonnet-4-20250514'
const MAX_TOOL_ITERATIONS = 10

// ============================================================================
// Types
// ============================================================================

/** Transient per-session runtime state (not persisted) */
interface SessionRuntime {
  cancelled: boolean
  abortController: AbortController | null
  messages: ChatMessage[]
}

/** ACP module bundle */
interface AcpModules {
  sessionStore: AcpSessionStore
  errorHandler: AcpErrorHandler
  modeManager: AcpModeManager
  mcpBridge: AcpMCPBridge
}

// ============================================================================
// Agent Handler
// ============================================================================

/** Create the Estela agent handler with ACP module integration */
function createEstelaAgent(connection: AgentSideConnection, modules: AcpModules): Agent {
  const { sessionStore, errorHandler, modeManager, mcpBridge } = modules
  const runtimes = new Map<string, SessionRuntime>()

  return {
    async initialize(_params: InitializeRequest): Promise<InitializeResponse> {
      return {
        agentInfo: { name: 'estela', version: VERSION },
        protocolVersion: PROTOCOL_VERSION,
        agentCapabilities: {},
      }
    },

    async newSession(params: NewSessionRequest): Promise<NewSessionResponse> {
      const sessionId = `session-${Date.now()}`

      // Persistent session via store (→ ~/.estela/sessions/)
      await sessionStore.create(sessionId, params.cwd)
      modeManager.initSession(sessionId)

      // Transient runtime state
      runtimes.set(sessionId, {
        cancelled: false,
        abortController: null,
        messages: [
          {
            role: 'system',
            content: buildSystemPrompt(params.cwd),
          },
        ],
      })

      // Connect editor-provided MCP servers
      const extParams = params as Record<string, unknown>
      const mcpConfigs = extParams.mcpServers as AcpMCPServerConfig[] | undefined
      if (mcpConfigs?.length) {
        const connected = await mcpBridge.connectServers(mcpConfigs)
        if (connected.length > 0) {
          console.error(`[Estela] Connected MCP servers: ${connected.join(', ')}`)
        }
      }

      return { sessionId }
    },

    async authenticate(_params: AuthenticateRequest): Promise<AuthenticateResponse> {
      return {}
    },

    async prompt(params: PromptRequest): Promise<PromptResponse> {
      const runtime = runtimes.get(params.sessionId)
      if (!runtime) {
        throw new AcpError(AcpErrorCode.SESSION_NOT_FOUND, `Session not found: ${params.sessionId}`)
      }

      runtime.cancelled = false
      runtime.abortController = new AbortController()

      // Extract text from prompt content blocks
      let promptText = ''
      for (const block of params.prompt) {
        if (block.type === 'text') {
          promptText += block.text
        }
      }

      // Handle mode switching commands
      const modeResult = handleModeCommand(
        promptText.trim(),
        params.sessionId,
        modeManager,
        connection
      )
      if (modeResult) return modeResult

      runtime.messages.push({ role: 'user', content: promptText })
      resetToolCallCount()

      // Filter tools by current mode
      const allTools = getToolDefinitions()
      const allowedTools = modeManager.getAllowedTools(params.sessionId)
      const tools = allowedTools ? allTools.filter((t) => allowedTools.includes(t.name)) : allTools

      const toolCtx: ToolContext = {
        sessionId: params.sessionId,
        workingDirectory: sessionStore.getInfo(params.sessionId)?.workingDirectory ?? '/tmp',
        signal: runtime.abortController.signal,
      }

      try {
        sendUpdate(connection, params.sessionId, 'agent_thought_chunk', 'Thinking...')

        const client = await createClient('anthropic')
        const config: ProviderConfig = {
          provider: 'anthropic',
          model: DEFAULT_MODEL,
          authMethod: 'api-key',
          maxTokens: 4096,
          tools,
        }

        const result = await runToolLoop(
          client,
          config,
          runtime,
          params.sessionId,
          toolCtx,
          connection,
          modeManager
        )

        // Persist session after successful prompt
        await sessionStore.save(params.sessionId)

        return result
      } catch (error) {
        return handlePromptError(error, params.sessionId, connection, errorHandler)
      } finally {
        runtime.abortController = null
      }
    },

    async cancel(params: CancelNotification): Promise<void> {
      const runtime = runtimes.get(params.sessionId)
      if (runtime) {
        runtime.cancelled = true
        runtime.abortController?.abort()
      }
    },
  }
}

// ============================================================================
// Tool Loop
// ============================================================================

/** Run the LLM → tool execution loop */
async function runToolLoop(
  client: Awaited<ReturnType<typeof createClient>>,
  config: ProviderConfig,
  runtime: SessionRuntime,
  sessionId: string,
  toolCtx: ToolContext,
  connection: AgentSideConnection,
  modeManager: AcpModeManager
): Promise<PromptResponse> {
  let toolIterations = 0

  while (toolIterations < MAX_TOOL_ITERATIONS) {
    toolIterations++

    let fullResponse = ''
    const pendingToolUses: ToolUseBlock[] = []

    for await (const delta of client.stream(
      runtime.messages,
      config,
      runtime.abortController!.signal
    )) {
      if (runtime.cancelled) break

      if (delta.error) {
        sendUpdate(
          connection,
          sessionId,
          'agent_message_chunk',
          `\n\nError: ${delta.error.message}`
        )
        break
      }

      if (delta.content) {
        fullResponse += delta.content
        sendUpdate(connection, sessionId, 'agent_message_chunk', delta.content)
      }

      if (delta.toolUse) pendingToolUses.push(delta.toolUse)

      if (delta.done && delta.usage) {
        console.error(
          `[Estela] Tokens: ${delta.usage.inputTokens} in, ${delta.usage.outputTokens} out`
        )
      }
    }

    if (fullResponse) {
      runtime.messages.push({ role: 'assistant', content: fullResponse })
    }

    if (runtime.cancelled) return { stopReason: 'cancelled' }
    if (pendingToolUses.length === 0) break

    // Execute tools with mode-based permission checks
    const results = await executeToolBatch(
      pendingToolUses,
      sessionId,
      toolCtx,
      connection,
      modeManager,
      runtime
    )
    if (results === null) return { stopReason: 'cancelled' }

    runtime.messages.push({
      role: 'user',
      content: `[Tool Results]\n${results}`,
    })
  }

  return { stopReason: 'end_turn' }
}

/** Execute a batch of tool calls, returning formatted results or null if cancelled */
async function executeToolBatch(
  toolUses: ToolUseBlock[],
  sessionId: string,
  toolCtx: ToolContext,
  connection: AgentSideConnection,
  modeManager: AcpModeManager,
  runtime: SessionRuntime
): Promise<string | null> {
  const parts: string[] = []

  for (const toolUse of toolUses) {
    // Check mode permissions
    if (!modeManager.isToolAllowed(sessionId, toolUse.name)) {
      parts.push(
        `Tool ${toolUse.id}: Error - '${toolUse.name}' is not allowed in plan mode. ` +
          'Switch to agent mode with /agent to use this tool.'
      )
      continue
    }

    sendUpdate(
      connection,
      sessionId,
      'agent_thought_chunk',
      `\nExecuting tool: ${toolUse.name}...\n`
    )

    const result = await executeTool(toolUse.name, toolUse.input, toolCtx)

    sendUpdate(
      connection,
      sessionId,
      'agent_message_chunk',
      `\n[Tool ${toolUse.name}]\n${result.output}\n`
    )

    parts.push(`Tool ${toolUse.id}: ${result.output}`)

    if (runtime.cancelled) return null
  }

  return parts.join('\n\n')
}

// ============================================================================
// Helpers
// ============================================================================

/** Handle /plan and /agent mode commands */
function handleModeCommand(
  text: string,
  sessionId: string,
  modeManager: AcpModeManager,
  connection: AgentSideConnection
): PromptResponse | null {
  if (text === '/plan') {
    modeManager.setMode(sessionId, 'plan')
    sendUpdate(
      connection,
      sessionId,
      'agent_message_chunk',
      'Switched to plan mode. Only read-only tools are available.'
    )
    return { stopReason: 'end_turn' }
  }
  if (text === '/agent') {
    modeManager.setMode(sessionId, 'agent')
    sendUpdate(
      connection,
      sessionId,
      'agent_message_chunk',
      'Switched to agent mode. All tools are available.'
    )
    return { stopReason: 'end_turn' }
  }
  return null
}

/** Handle errors during prompt execution */
async function handlePromptError(
  error: unknown,
  sessionId: string,
  connection: AgentSideConnection,
  errorHandler: AcpErrorHandler
): Promise<PromptResponse> {
  if (errorHandler.isDisconnectError(error)) {
    await errorHandler.handleDisconnect()
    return { stopReason: 'cancelled' }
  }

  const formatted = await errorHandler.handleError(error, `prompt:${sessionId}`)

  if (error instanceof Error && error.name === 'AbortError') {
    return { stopReason: 'cancelled' }
  }

  sendUpdate(connection, sessionId, 'agent_message_chunk', `\n\nError: ${formatted.message}`)
  return { stopReason: 'end_turn' }
}

function buildSystemPrompt(cwd: string): string {
  return `You are Estela, an AI coding assistant. You are helping a developer in their project located at: ${cwd}

You have access to tools for file operations and shell commands. Use them to help the developer.

Be concise but helpful. When asked to perform tasks, use the appropriate tools and explain what you're doing.`
}

/** Send a session update notification */
function sendUpdate(
  connection: AgentSideConnection,
  sessionId: string,
  sessionUpdate: 'agent_message_chunk' | 'agent_thought_chunk',
  text: string
): void {
  connection.sessionUpdate({
    sessionId,
    update: {
      sessionUpdate,
      content: { type: 'text', text },
    } as SessionNotification['update'],
  })
}

// ============================================================================
// Stream Conversion
// ============================================================================

/** Convert Node.js streams to Web Streams for ACP ndjson transport */
function nodeToWebStreams(): {
  readable: ReadableStream<Uint8Array>
  writable: WritableStream<Uint8Array>
} {
  const readable = new ReadableStream<Uint8Array>({
    start(controller) {
      process.stdin.on('data', (chunk: Buffer) => {
        controller.enqueue(new Uint8Array(chunk))
      })
      process.stdin.on('end', () => controller.close())
      process.stdin.on('error', (err) => controller.error(err))
    },
  })

  const writable = new WritableStream<Uint8Array>({
    write(chunk) {
      return new Promise((resolve, reject) => {
        process.stdout.write(chunk, (err) => {
          if (err) reject(err)
          else resolve()
        })
      })
    },
  })

  return { readable, writable }
}

// ============================================================================
// Entry Point
// ============================================================================

/** Start the ACP agent - main entry point */
export async function startAcpAgent(): Promise<void> {
  // Initialize ACP modules
  const sessionStore = createAcpSessionStore()
  const errorHandler = createAcpErrorHandler()
  const modeManager = createAcpModeManager()
  const mcpBridge = createAcpMCPBridge()

  // Wire modules
  errorHandler.setSessionStore(sessionStore)

  // Detect stdin close as editor disconnect
  process.stdin.on('close', () => {
    errorHandler.handleDisconnect().catch(() => {})
  })

  const { readable, writable } = nodeToWebStreams()
  const stream = ndJsonStream(writable, readable)

  const connection = new AgentSideConnection(
    (conn) => createEstelaAgent(conn, { sessionStore, errorHandler, modeManager, mcpBridge }),
    stream
  )

  await connection.closed

  // Graceful cleanup
  await Promise.all([sessionStore.dispose(), mcpBridge.dispose(), errorHandler.dispose()])
}
