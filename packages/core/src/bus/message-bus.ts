/**
 * Message Bus
 * Event-driven communication layer between policy engine, tools, and UI.
 *
 * Features:
 * - Publish/subscribe with typed messages
 * - Request/response with correlation IDs and timeout
 * - Integrated tool confirmation flow (policy engine → UI)
 * - Decouples tool execution from approval UI
 *
 * Flow for tool confirmation:
 * 1. Tool registry publishes TOOL_CONFIRMATION_REQUEST
 * 2. Message bus checks policy engine
 * 3. If ALLOW → auto-responds with confirmed=true
 * 4. If DENY → emits TOOL_POLICY_REJECTION + responds confirmed=false
 * 5. If ASK_USER → re-emits to UI subscribers, waits for response
 */

import { randomUUID } from 'node:crypto'
import type { RiskLevel } from '../permissions/types.js'
import type { PolicyEngine } from '../policy/engine.js'
import {
  type AnyBusMessage,
  type BusMessage,
  BusMessageType,
  type ToolConfirmationRequest,
  type ToolConfirmationResponse,
  type ToolExecutionFailure,
  type ToolExecutionStart,
  type ToolExecutionSuccess,
  type ToolPolicyRejection,
} from './types.js'

// ============================================================================
// Types
// ============================================================================

type MessageHandler<T extends BusMessage = BusMessage> = (message: T) => void
type Unsubscribe = () => void

// ============================================================================
// Message Bus
// ============================================================================

export class MessageBus {
  private listeners = new Map<BusMessageType, Set<MessageHandler>>()
  private policyEngine: PolicyEngine | null

  constructor(policyEngine?: PolicyEngine) {
    this.policyEngine = policyEngine ?? null
  }

  // ==========================================================================
  // Publish/Subscribe
  // ==========================================================================

  /**
   * Subscribe to a message type.
   * Returns an unsubscribe function.
   */
  subscribe<T extends AnyBusMessage>(type: T['type'], handler: MessageHandler<T>): Unsubscribe {
    if (!this.listeners.has(type)) {
      this.listeners.set(type, new Set())
    }

    const handlers = this.listeners.get(type)!
    handlers.add(handler as MessageHandler)

    return () => {
      handlers.delete(handler as MessageHandler)
      if (handlers.size === 0) {
        this.listeners.delete(type)
      }
    }
  }

  /**
   * Publish a message to all subscribers of its type.
   */
  publish(message: AnyBusMessage): void {
    const handlers = this.listeners.get(message.type)
    if (!handlers) return

    for (const handler of handlers) {
      try {
        handler(message)
      } catch (error) {
        console.error(`[MessageBus] Handler error for ${message.type}:`, error)
      }
    }
  }

  // ==========================================================================
  // Request/Response
  // ==========================================================================

  /**
   * Send a request and wait for a correlated response.
   *
   * @param request - Request message (correlationId and timestamp auto-generated)
   * @param responseType - Expected response message type
   * @param timeoutMs - Timeout in milliseconds (default 60000)
   * @returns The correlated response message
   */
  async request<TReq extends AnyBusMessage, TRes extends AnyBusMessage>(
    request: Omit<TReq, 'correlationId' | 'timestamp'>,
    responseType: TRes['type'],
    timeoutMs = 60_000
  ): Promise<TRes> {
    const correlationId = randomUUID()
    const fullRequest = {
      ...request,
      correlationId,
      timestamp: Date.now(),
    } as TReq

    return new Promise<TRes>((resolve, reject) => {
      let timeoutHandle: ReturnType<typeof setTimeout>
      let unsubscribe: Unsubscribe

      // Set up response listener
      unsubscribe = this.subscribe<TRes>(responseType, (response) => {
        if (response.correlationId === correlationId) {
          clearTimeout(timeoutHandle)
          unsubscribe()
          resolve(response)
        }
      })

      // Set up timeout
      timeoutHandle = setTimeout(() => {
        unsubscribe()
        reject(
          new Error(
            `MessageBus request timeout (${timeoutMs}ms) for ${String(request.type)} → ${String(responseType)}`
          )
        )
      }, timeoutMs)

      // Publish the request
      this.publish(fullRequest)
    })
  }

  // ==========================================================================
  // Tool Confirmation Flow
  // ==========================================================================

  /**
   * Confirm tool execution through the policy engine and message bus.
   *
   * This is the main integration point between tools and the approval UI:
   * 1. Policy engine evaluates the tool call
   * 2. ALLOW → returns immediately
   * 3. DENY → emits rejection, returns denied
   * 4. ASK_USER → emits to UI, waits for response
   *
   * @param toolName - Tool being called
   * @param toolArgs - Tool arguments
   * @param riskLevel - Risk assessment of the operation
   * @param description - Human-readable description
   * @returns Whether the tool execution is confirmed
   */
  async confirmToolExecution(
    toolName: string,
    toolArgs: Record<string, unknown>,
    riskLevel: RiskLevel = 'medium',
    description?: string
  ): Promise<{
    confirmed: boolean
    reason: string
    remember?: 'session' | 'persistent' | false
  }> {
    // No policy engine → auto-approve (backwards compatible)
    if (!this.policyEngine) {
      return { confirmed: true, reason: 'No policy engine configured' }
    }

    // Evaluate policy
    const decision = await this.policyEngine.check(toolName, toolArgs)

    switch (decision.decision) {
      case 'allow':
        return { confirmed: true, reason: decision.reason }

      case 'deny': {
        // Emit rejection event
        this.publish({
          type: BusMessageType.TOOL_POLICY_REJECTION,
          correlationId: randomUUID(),
          timestamp: Date.now(),
          toolName,
          toolArgs,
          reason: decision.reason,
          denyMessage: decision.denyMessage,
        } satisfies ToolPolicyRejection)

        return {
          confirmed: false,
          reason: decision.denyMessage ?? decision.reason,
        }
      }

      case 'ask_user': {
        // Check if anyone is listening for confirmations
        const hasListeners = this.listeners.has(BusMessageType.TOOL_CONFIRMATION_REQUEST)

        if (!hasListeners) {
          // No UI connected → auto-approve for backwards compatibility
          return { confirmed: true, reason: 'No confirmation UI connected' }
        }

        // Request confirmation from UI
        try {
          const response = await this.request<ToolConfirmationRequest, ToolConfirmationResponse>(
            {
              type: BusMessageType.TOOL_CONFIRMATION_REQUEST,
              toolName,
              toolArgs,
              riskLevel,
              description,
            } as Omit<ToolConfirmationRequest, 'correlationId' | 'timestamp'>,
            BusMessageType.TOOL_CONFIRMATION_RESPONSE,
            60_000 // 60s timeout for user response
          )

          return {
            confirmed: response.confirmed,
            reason: response.confirmed ? 'User approved' : (response.denyReason ?? 'User denied'),
            remember: response.rememberChoice,
          }
        } catch {
          // Timeout → deny
          return {
            confirmed: false,
            reason: 'User confirmation timed out',
          }
        }
      }

      default:
        return { confirmed: false, reason: 'Unknown policy decision' }
    }
  }

  // ==========================================================================
  // Convenience Publishers
  // ==========================================================================

  /**
   * Notify that a tool has started execution.
   */
  notifyToolStart(toolName: string, toolArgs: Record<string, unknown>): void {
    this.publish({
      type: BusMessageType.TOOL_EXECUTION_START,
      correlationId: randomUUID(),
      timestamp: Date.now(),
      toolName,
      toolArgs,
    } satisfies ToolExecutionStart)
  }

  /**
   * Notify that a tool has completed successfully.
   */
  notifyToolSuccess(toolName: string, durationMs: number, outputPreview?: string): void {
    this.publish({
      type: BusMessageType.TOOL_EXECUTION_SUCCESS,
      correlationId: randomUUID(),
      timestamp: Date.now(),
      toolName,
      durationMs,
      outputPreview,
    } satisfies ToolExecutionSuccess)
  }

  /**
   * Notify that a tool has failed.
   */
  notifyToolFailure(toolName: string, error: string, durationMs: number): void {
    this.publish({
      type: BusMessageType.TOOL_EXECUTION_FAILURE,
      correlationId: randomUUID(),
      timestamp: Date.now(),
      toolName,
      error,
      durationMs,
    } satisfies ToolExecutionFailure)
  }

  // ==========================================================================
  // Lifecycle
  // ==========================================================================

  /**
   * Check if there are any subscribers for a message type.
   */
  hasSubscribers(type: BusMessageType): boolean {
    const handlers = this.listeners.get(type)
    return handlers !== undefined && handlers.size > 0
  }

  /**
   * Remove all subscribers.
   */
  clear(): void {
    this.listeners.clear()
  }

  /**
   * Set or replace the policy engine.
   */
  setPolicyEngine(engine: PolicyEngine): void {
    this.policyEngine = engine
  }
}

// ============================================================================
// Singleton
// ============================================================================

let defaultBus: MessageBus | null = null

/**
 * Get the default message bus instance.
 */
export function getMessageBus(): MessageBus {
  if (!defaultBus) {
    defaultBus = new MessageBus()
  }
  return defaultBus
}

/**
 * Set the default message bus (for testing or custom configuration).
 */
export function setMessageBus(bus: MessageBus): void {
  defaultBus = bus
}

/**
 * Reset the default message bus.
 */
export function resetMessageBus(): void {
  defaultBus = null
}
