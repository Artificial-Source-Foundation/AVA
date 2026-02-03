/**
 * Estela ACP Agent
 *
 * Implements the Agent Client Protocol for integration with
 * Toad, Zed, and other ACP-compatible editors.
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
  type ChatMessage,
  createClient,
  executeTool,
  getToolDefinitions,
  type ProviderConfig,
  resetToolCallCount,
  type ToolContext,
  type ToolUseBlock,
} from '@estela/core'

const VERSION = '0.1.0'
// Protocol version is a number format: YYYYMMDD
const PROTOCOL_VERSION = 20250101

// Default model to use
const DEFAULT_MODEL = 'claude-sonnet-4-20250514'

// Maximum tool loop iterations to prevent infinite loops
const MAX_TOOL_ITERATIONS = 10

/** Session state tracking */
interface SessionState {
  id: string
  workingDir: string
  cancelled: boolean
  abortController: AbortController | null
  messages: ChatMessage[]
}

/** Active sessions */
const sessions = new Map<string, SessionState>()

/** Tool result for message history */
interface ToolResultMessage {
  type: 'tool_result'
  tool_use_id: string
  content: string
  is_error?: boolean
}

/** Create the Estela agent handler */
function createEstelaAgent(connection: AgentSideConnection): Agent {
  return {
    async initialize(_params: InitializeRequest): Promise<InitializeResponse> {
      return {
        agentInfo: {
          name: 'estela',
          version: VERSION,
        },
        protocolVersion: PROTOCOL_VERSION,
        agentCapabilities: {},
      }
    },

    async newSession(params: NewSessionRequest): Promise<NewSessionResponse> {
      const sessionId = `session-${Date.now()}`

      sessions.set(sessionId, {
        id: sessionId,
        workingDir: params.cwd,
        cancelled: false,
        abortController: null,
        messages: [
          {
            role: 'system',
            content: `You are Estela, an AI coding assistant. You are helping a developer in their project located at: ${params.cwd}

You have access to tools for file operations and shell commands. Use them to help the developer.

Be concise but helpful. When asked to perform tasks, use the appropriate tools and explain what you're doing.`,
          },
        ],
      })

      return {
        sessionId,
      }
    },

    async authenticate(_params: AuthenticateRequest): Promise<AuthenticateResponse> {
      // No authentication required for now
      return {}
    },

    async prompt(params: PromptRequest): Promise<PromptResponse> {
      const session = sessions.get(params.sessionId)
      if (!session) {
        throw new Error(`Session not found: ${params.sessionId}`)
      }

      session.cancelled = false
      session.abortController = new AbortController()

      // Extract text from prompt content blocks
      let promptText = ''
      for (const block of params.prompt) {
        if (block.type === 'text') {
          promptText += block.text
        }
      }

      // Add user message to history
      session.messages.push({
        role: 'user',
        content: promptText,
      })

      // Reset tool call counter for this turn
      resetToolCallCount()

      // Get tool definitions
      const tools = getToolDefinitions()

      // Create tool context
      const toolCtx: ToolContext = {
        sessionId: params.sessionId,
        workingDirectory: session.workingDir,
        signal: session.abortController.signal,
      }

      try {
        // Send thinking update
        sendSessionUpdate(connection, params.sessionId, {
          sessionUpdate: 'agent_thought_chunk',
          content: {
            type: 'text',
            text: 'Thinking...',
          },
        })

        // Create LLM client and stream response
        const client = await createClient('anthropic')
        const config: ProviderConfig = {
          provider: 'anthropic',
          model: DEFAULT_MODEL,
          authMethod: 'api-key',
          maxTokens: 4096,
          tools, // Pass tools to LLM
        }

        let toolIterations = 0

        // Tool loop - continue until no more tool calls or max iterations
        while (toolIterations < MAX_TOOL_ITERATIONS) {
          toolIterations++

          let fullResponse = ''
          const pendingToolUses: ToolUseBlock[] = []

          for await (const delta of client.stream(
            session.messages,
            config,
            session.abortController.signal
          )) {
            if (session.cancelled) {
              break
            }

            if (delta.error) {
              sendSessionUpdate(connection, params.sessionId, {
                sessionUpdate: 'agent_message_chunk',
                content: {
                  type: 'text',
                  text: `\n\nError: ${delta.error.message}`,
                },
              })
              break
            }

            // Handle text content
            if (delta.content) {
              fullResponse += delta.content
              sendSessionUpdate(connection, params.sessionId, {
                sessionUpdate: 'agent_message_chunk',
                content: {
                  type: 'text',
                  text: delta.content,
                },
              })
            }

            // Collect tool use requests
            if (delta.toolUse) {
              pendingToolUses.push(delta.toolUse)
            }

            if (delta.done && delta.usage) {
              // Log token usage
              console.error(
                `[Estela] Tokens: ${delta.usage.inputTokens} in, ${delta.usage.outputTokens} out`
              )
            }
          }

          // Add assistant response to history (text portion)
          if (fullResponse) {
            session.messages.push({
              role: 'assistant',
              content: fullResponse,
            })
          }

          // Check cancellation
          if (session.cancelled) {
            return { stopReason: 'cancelled' }
          }

          // If no tool calls, we're done
          if (pendingToolUses.length === 0) {
            break
          }

          // Execute tools and collect results
          const toolResults: ToolResultMessage[] = []

          for (const toolUse of pendingToolUses) {
            // Notify about tool execution via thought chunk
            sendSessionUpdate(connection, params.sessionId, {
              sessionUpdate: 'agent_thought_chunk',
              content: {
                type: 'text',
                text: `\nExecuting tool: ${toolUse.name}...\n`,
              },
            })

            // Execute tool
            const result = await executeTool(toolUse.name, toolUse.input, toolCtx)

            // Show tool result via message chunk
            sendSessionUpdate(connection, params.sessionId, {
              sessionUpdate: 'agent_message_chunk',
              content: {
                type: 'text',
                text: `\n[Tool ${toolUse.name}]\n${result.output}\n`,
              },
            })

            toolResults.push({
              type: 'tool_result',
              tool_use_id: toolUse.id,
              content: result.output,
              is_error: !result.success,
            })

            // Check cancellation between tools
            if (session.cancelled) {
              return { stopReason: 'cancelled' }
            }
          }

          // Add tool results to messages for next iteration
          // Note: This is a simplified approach - in practice, we'd need to
          // properly format the tool results as expected by the LLM API
          const toolResultsContent = toolResults
            .map((r) => `Tool ${r.tool_use_id}: ${r.content}`)
            .join('\n\n')

          session.messages.push({
            role: 'user',
            content: `[Tool Results]\n${toolResultsContent}`,
          })
        }

        return { stopReason: 'end_turn' }
      } catch (error) {
        if (error instanceof Error && error.name === 'AbortError') {
          return { stopReason: 'cancelled' }
        }

        // Send error as a message chunk
        sendSessionUpdate(connection, params.sessionId, {
          sessionUpdate: 'agent_message_chunk',
          content: {
            type: 'text',
            text: `\n\nError: ${error instanceof Error ? error.message : 'Unknown error'}`,
          },
        })
        return { stopReason: 'end_turn' }
      } finally {
        session.abortController = null
      }
    },

    async cancel(params: CancelNotification): Promise<void> {
      const session = sessions.get(params.sessionId)
      if (session) {
        session.cancelled = true
        session.abortController?.abort()
      }
    },
  }
}

/** Send session update notification */
function sendSessionUpdate(
  connection: AgentSideConnection,
  sessionId: string,
  update: SessionNotification['update']
): void {
  connection.sessionUpdate({
    sessionId,
    update,
  })
}

/** Convert Node.js streams to Web Streams */
function nodeToWebStreams(): {
  readable: ReadableStream<Uint8Array>
  writable: WritableStream<Uint8Array>
} {
  // Convert stdin to ReadableStream
  const readable = new ReadableStream<Uint8Array>({
    start(controller) {
      process.stdin.on('data', (chunk: Buffer) => {
        controller.enqueue(new Uint8Array(chunk))
      })
      process.stdin.on('end', () => {
        controller.close()
      })
      process.stdin.on('error', (err) => {
        controller.error(err)
      })
    },
  })

  // Convert stdout to WritableStream
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

/** Start the ACP agent - main entry point */
export async function startAcpAgent(): Promise<void> {
  // Convert Node.js streams to Web Streams
  const { readable, writable } = nodeToWebStreams()

  // Create ndjson stream
  const stream = ndJsonStream(writable, readable)

  // Create agent connection
  const connection = new AgentSideConnection(createEstelaAgent, stream)

  // Wait for connection to close
  await connection.closed
}
