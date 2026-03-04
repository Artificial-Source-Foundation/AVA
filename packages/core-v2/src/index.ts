/**
 * @ava/core-v2 — Minimal core with extension-first architecture.
 *
 * ~28 files, ~4,500 lines. Everything else lives in packages/extensions/.
 */

export type {
  AgentConfig,
  AgentEvent,
  AgentEventCallback,
  AgentInputs,
  AgentResult,
  ToolCallInfo,
} from './agent/index.js'
// Agent
export { AgentExecutor, AgentTerminateMode, runAgent } from './agent/index.js'
export type { BusMessage, MessageHandler, Unsubscribe } from './bus/index.js'
// Bus
export { getMessageBus, MessageBus, resetMessageBus, setMessageBus } from './bus/index.js'
export type { AgentSettings, ProviderSettings, SettingsEvent } from './config/index.js'

// Config
export {
  getSettingsManager,
  resetSettingsManager,
  SettingsManager,
  setSettingsManager,
} from './config/index.js'
export type {
  AgentMode,
  ContextStrategy,
  Disposable,
  EventHandler,
  Extension,
  ExtensionAPI,
  ExtensionManifest,
  ExtensionModule,
  ExtensionStorage,
  LLMClientFactory,
  SlashCommand,
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
  ValidationResult,
  Validator,
} from './extensions/index.js'
// Extensions
export {
  createExtensionAPI,
  ExtensionManager,
  emitEvent,
  getAgentModes,
  getCommands,
  getContextStrategies,
  getToolMiddlewares,
  getValidators,
  loadBuiltInExtension,
  loadExtensionsFromDirectory,
  onEvent,
  resetRegistries,
} from './extensions/index.js'
export type {
  AuthInfo,
  ChatMessage,
  LLMClient,
  LLMProvider,
  ProviderConfig,
  StreamDelta,
  StreamError,
  TokenUsage,
  ToolDefinition,
  ToolUseBlock,
} from './llm/index.js'

// LLM
export {
  createClient,
  getApiKey,
  getAuth,
  getRegisteredProviders,
  hasProvider,
  registerProvider,
  resetProviders,
  unregisterProvider,
} from './llm/index.js'
export type { LogEntry, LoggerConfig, LogLevel, SimpleLogger } from './logger/index.js'
// Logger
export { configureLogger, createLogger, getLoggerConfig, resetLogger } from './logger/index.js'
export type {
  ChildProcess,
  DirEntry,
  ExecOptions,
  ExecResult,
  FileStat,
  ICredentialStore,
  IDatabase,
  IFileSystem,
  INativeCompute,
  IPlatformProvider,
  IPTY,
  IShell,
  Migration,
  NativeFuzzyReplaceInput,
  NativeFuzzyReplaceOutput,
  NativeGrepInput,
  NativeGrepMatch,
  NativeGrepOutput,
  PTYOptions,
  PTYProcess,
  SpawnOptions,
} from './platform.js'
// Platform
export { getPlatform, setPlatform } from './platform.js'
export type {
  FileState,
  SessionEvent,
  SessionMeta,
  SessionState,
  SessionStatus,
  TokenStats,
} from './session/index.js'
// Session
export { createSessionManager, SessionManager } from './session/index.js'
export type { AnyTool, Tool, ToolContext, ToolLocation, ToolResult } from './tools/index.js'
// Tools
export {
  defineTool,
  executeTool,
  getAllTools,
  getTool,
  getToolDefinitions,
  registerCoreTools,
  registerTool,
  resetTools,
  ToolError,
  ToolErrorType,
  unregisterTool,
} from './tools/index.js'
