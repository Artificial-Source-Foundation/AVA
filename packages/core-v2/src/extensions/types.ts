/**
 * Extension system types.
 *
 * Extensions are the primary way to add functionality to AVA.
 * Built-in extensions use the same API as community extensions.
 */

import type { MessageBus } from '../bus/message-bus.js'
import type { ToolDefinition } from '../llm/types.js'
import type { SimpleLogger } from '../logger/types.js'
import type { IPlatformProvider } from '../platform.js'
import type { SessionManager } from '../session/manager.js'
import type { Tool, ToolContext, ToolResult } from '../tools/types.js'

// ─── Disposable ──────────────────────────────────────────────────────────────

/** Returned by registration methods. Call `dispose()` to unregister. */
export interface Disposable {
  dispose(): void
}

// ─── Extension Manifest ──────────────────────────────────────────────────────

export interface ExtensionManifest {
  name: string
  version: string
  description?: string
  main: string
  builtIn?: boolean
  enabledByDefault?: boolean
  priority?: number
  capabilities?: ExtensionCapability[]
  settings?: Record<string, SettingDefinition>
  dependencies?: string[]
}

export type ExtensionCapability =
  | 'tools'
  | 'commands'
  | 'agent-modes'
  | 'validators'
  | 'context-strategies'
  | 'providers'
  | 'tool-middleware'
  | 'settings'

export interface SettingDefinition {
  type: 'string' | 'number' | 'boolean' | 'array' | 'object'
  default: unknown
  description?: string
}

// ─── Extension ───────────────────────────────────────────────────────────────

export interface Extension {
  manifest: ExtensionManifest
  path: string
  isActive: boolean
}

export type ExtensionActivator = (
  api: ExtensionAPI
) => undefined | Disposable | Promise<undefined | Disposable>

export interface ExtensionModule {
  activate: ExtensionActivator
}

// ─── Extension Events ────────────────────────────────────────────────────────

export type ExtensionEvent =
  | { type: 'activated'; name: string }
  | { type: 'deactivated'; name: string }
  | { type: 'error'; name: string; error: string }
  | { type: 'loaded'; count: number }

export type ExtensionEventListener = (event: ExtensionEvent) => void

// ─── Tool Middleware ─────────────────────────────────────────────────────────

export interface ToolMiddlewareContext {
  toolName: string
  args: Record<string, unknown>
  ctx: ToolContext
  definition: ToolDefinition
}

export interface ToolMiddlewareResult {
  /** If true, tool execution is blocked. */
  blocked?: boolean
  /** Reason for blocking (shown to agent). */
  reason?: string
  /** Modified args (replaces original if provided). */
  args?: Record<string, unknown>
  /** Modified result (replaces original if provided). Post-middleware only. */
  result?: ToolResult
}

export interface ToolMiddleware {
  name: string
  priority: number
  before?(context: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined>
  after?(
    context: ToolMiddlewareContext,
    result: ToolResult
  ): Promise<ToolMiddlewareResult | undefined>
}

// ─── Agent Modes ─────────────────────────────────────────────────────────────

export interface AgentMode {
  name: string
  description: string
  /** Filter which tools are available in this mode. */
  filterTools?(tools: ToolDefinition[]): ToolDefinition[]
  /** Modify the system prompt for this mode. */
  systemPrompt?(base: string): string
  /** Called when mode is entered. */
  onEnter?(): void | Promise<void>
  /** Called when mode is exited. */
  onExit?(): void | Promise<void>
}

// ─── Validators ──────────────────────────────────────────────────────────────

export interface ValidationResult {
  passed: boolean
  errors: string[]
  warnings: string[]
}

export interface Validator {
  name: string
  description: string
  validate(files: string[], cwd: string, signal: AbortSignal): Promise<ValidationResult>
}

// ─── Context Strategies ──────────────────────────────────────────────────────

export interface ContextStrategy {
  name: string
  description: string
  compact(messages: ChatMessage[], targetTokens: number): ChatMessage[]
}

import type { ChatMessage } from '../llm/types.js'

// ─── Slash Commands ──────────────────────────────────────────────────────────

export interface SlashCommand {
  name: string
  description: string
  execute(args: string, ctx: ToolContext): Promise<string>
}

// ─── Hooks ──────────────────────────────────────────────────────────────────

/** Well-known hook names (extensible via string). */
export type HookName =
  | 'prompt:transform'
  | 'tool:describe'
  | 'tool:beforeExecute'
  | 'tool:afterExecute'
  | 'history:process'
  | 'message:preProcess'
  | 'message:postProcess'
  | 'completion:validate'
  | (string & {})

/** A hook handler receives input and current output, returns modified output. */
export type HookHandler<TInput = unknown, TOutput = unknown> = (
  input: TInput,
  output: TOutput
) => TOutput | Promise<TOutput>

/** Result of calling a hook chain. */
export interface HookResult<TOutput = unknown> {
  output: TOutput
  /** Number of handlers that ran. */
  handlerCount: number
}

// ─── Extension Storage ───────────────────────────────────────────────────────

export interface ExtensionStorage {
  get<T>(key: string): Promise<T | null>
  set<T>(key: string, value: T): Promise<void>
  delete(key: string): Promise<void>
  keys(): Promise<string[]>
}

// ─── Extension API ───────────────────────────────────────────────────────────

export type EventHandler = (data: unknown) => undefined | void | Promise<void>

/**
 * The contract all extensions code against.
 *
 * Extensions receive this in their `activate()` function. All registration
 * methods return a `Disposable` — call `dispose()` to unregister.
 */
export interface ExtensionAPI {
  // Tool registration
  registerTool(tool: Tool): Disposable

  // Slash command registration
  registerCommand(command: SlashCommand): Disposable

  // Agent mode registration (plan mode, team mode, minimal mode)
  registerAgentMode(mode: AgentMode): Disposable

  // Validator registration
  registerValidator(validator: Validator): Disposable

  // Context strategy registration
  registerContextStrategy(strategy: ContextStrategy): Disposable

  // LLM provider registration
  registerProvider(name: string, factory: LLMClientFactory): Disposable

  // Tool middleware (intercept tool execution pipeline)
  addToolMiddleware(middleware: ToolMiddleware): Disposable

  // Hooks (sequential chaining pipeline)
  registerHook<TInput = unknown, TOutput = unknown>(
    name: HookName,
    handler: HookHandler<TInput, TOutput>
  ): Disposable
  callHook<TInput = unknown, TOutput = unknown>(
    name: HookName,
    input: TInput,
    output: TOutput
  ): Promise<HookResult<TOutput>>

  // Events
  on(event: string, handler: EventHandler): Disposable
  emit(event: string, data: unknown): void

  // Settings (extension-scoped)
  getSettings<T>(namespace: string): T
  onSettingsChanged(namespace: string, cb: (settings: unknown) => void): Disposable

  // Infrastructure
  readonly bus: MessageBus
  readonly log: SimpleLogger
  readonly platform: IPlatformProvider
  readonly storage: ExtensionStorage

  getSessionManager(): SessionManager
}

// ─── LLM Client Factory ─────────────────────────────────────────────────────

import type { LLMClient } from '../llm/types.js'

export type LLMClientFactory = () => LLMClient
