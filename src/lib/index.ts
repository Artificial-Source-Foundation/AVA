/**
 * Delta9 Library Exports
 *
 * Re-exports all library utilities.
 */

// Path utilities
export {
  DELTA9_DIR,
  MISSION_FILE,
  MISSION_MD,
  HISTORY_FILE,
  CONFIG_FILE,
  MEMORY_FILE,
  CHECKPOINTS_DIR,
  GLOBAL_CONFIG_DIR,
  GLOBAL_CONFIG_FILE,
  getDelta9Dir,
  getMissionPath,
  getMissionMdPath,
  getHistoryPath,
  getProjectConfigPath,
  getMemoryPath,
  getCheckpointsDir,
  getCheckpointPath,
  getGlobalConfigPath,
  ensureDelta9Dir,
  ensureCheckpointsDir,
  ensureGlobalConfigDir,
  isDelta9Initialized,
  missionExists,
  projectConfigExists,
  globalConfigExists,
  checkpointExists,
} from './paths.js'

// Configuration
export {
  loadConfig,
  getConfig,
  clearConfigCache,
  reloadConfig,
  getCommanderConfig,
  getCouncilConfig,
  getOperatorConfig,
  getValidatorConfig,
  getBudgetConfig,
  getMissionSettings,
  getSeamlessConfig,
  isCouncilEnabled,
  getEnabledOracles,
  isBudgetEnabled,
  getBudgetLimit,
} from './config.js'

// Logger
export {
  type LogLevel,
  type Logger,
  type OpenCodeClient,
  type Delta9LogContext,
  createLogger,
  setDefaultLogger,
  getLogger,
  getNamedLogger,
  initLogger,
  debug,
  info,
  warn,
  error,
} from './logger.js'

// Errors
export {
  Delta9Error,
  type Delta9ErrorOptions,
  errors,
  isDelta9Error,
  formatErrorResponse,
} from './errors.js'

// Hints
export {
  hints,
  type HintContext,
  getHint,
  getBackgroundListHint,
  getMissionStatusHint,
  getCouncilStatusHint,
} from './hints.js'

// Background Manager
export {
  BackgroundManager,
  getBackgroundManager,
  clearBackgroundManager,
  type BackgroundTask,
  type BackgroundTaskStatus,
  type BackgroundManagerConfig,
  type LaunchInput,
  type ExecuteSyncInput,
} from './background-manager.js'

// Model Resolution
export {
  getModelForRole,
  getEnabledOracleConfigs,
  getSupportAgentModel,
  parseModelId,
  buildModelId,
  getFallbackChain,
  resolveModelWithFallback,
  getCouncilModels,
  autoDetectCouncilMode,
  type ModelRole,
  type SupportAgentType,
  type TaskComplexity,
} from './models.js'

// Budget
export {
  BudgetManager,
  createBudgetManager,
  formatBudget,
  describeBudgetStatus,
  MODEL_COSTS,
  type BudgetConfig,
  type BudgetStatus,
  type BudgetCheckResult,
  type AgentCategory,
} from './budget.js'

// Rate Limiter
export {
  RateLimiter,
  createRateLimiter,
  getBestFallback,
  describeRetryResult,
  FALLBACK_MODELS,
  type RateLimitConfig,
  type RateLimitError,
  type RetryResult,
} from './rate-limiter.js'

// Concurrency Manager
export {
  ProviderConcurrencyManager,
  getConcurrencyManager,
  clearConcurrencyManager,
  withConcurrencySlot,
  describeConcurrencyStatus,
  type ConcurrencyConfig,
  type ConcurrencySlot,
  type ConcurrencyStatus,
} from './concurrency-manager.js'

// Process Cleanup
export {
  ProcessCleanupManager,
  getCleanupManager,
  registerCleanup,
  unregisterCleanup,
  shutdown,
  CleanupPriority,
  type CleanupHandler,
  type CleanupConfig,
} from './process-cleanup.js'

// Event Store
export {
  EventStore,
  getEventStore,
  clearEventStore,
  historyToVersionedEvent,
  importHistoryEvents,
  type VersionedEvent,
  type Snapshot,
  type EventFilter,
  type EventStoreConfig,
} from './event-store.js'

// Confidence Levels
export {
  CONFIDENCE,
  getConfidenceLabel,
  isHighConfidence,
  meetsMinimumConfidence,
  clampConfidence,
  type ConfidenceLevel,
} from './confidence-levels.js'

// Task Statuses
export {
  BACKGROUND_STATUS,
  ALL_BACKGROUND_STATUSES,
  isTerminalStatus,
  isActiveStatus,
  isSuccessStatus,
  isFailureStatus,
  type BackgroundStatus,
} from './task-statuses.js'

// Guard Formatting
export {
  formatGuardViolation as formatGuardViolationShared,
  createCommanderViolation,
  createOperatorViolation,
  type GuardType,
  type GuardViolationContext,
} from './guard-formatting.js'

// Version
export { getVersion, resetVersionCache, getVersionWithPrefix } from './version.js'

// Tool Response
export {
  success as toolSuccess,
  error as toolError,
  fromDelta9Error,
  isSuccessResponse,
  isErrorResponse,
  parseResponse,
  type ToolSuccessResponse,
  type ToolErrorResponse,
  type ToolResponse,
  type ErrorResponseOptions,
} from './tool-response.js'

// Session Isolation
export {
  SessionIsolationManager,
  getSessionIsolationManager,
  clearSessionIsolationManager,
  registerSession,
  getRootSession,
  areSessionsRelated,
  cleanupSessionTree,
  type SessionInfo,
  type SessionStats,
} from './session-isolation.js'

// Injection Tracker
export {
  InjectionTracker,
  getInjectionTracker,
  clearInjectionTracker,
  hasInjected,
  tryInject,
  clearSessionInjections,
  CONTEXT_TYPES,
  type InjectionRecord,
  type InjectionStats,
  type ContextType,
} from './injection-tracker.js'

// Storage Adapter
export {
  FileStorageAdapter,
  MemoryStorageAdapter,
  getStorageAdapter,
  setStorageAdapter,
  clearStorageAdapter,
  createFileStorage,
  createMemoryStorage,
  type StorageAdapter,
  type StorageOptions,
  type ReadOptions,
  type WriteOptions,
} from './storage-adapter.js'

// Semantic Search
export {
  semanticSearch,
  searchMemoryBlocks,
  rerankResults,
  filterByScore,
  topResults,
  parseQuery,
  normalizeText,
  tokenize,
  extractTags,
  DEFAULT_SEARCH_CONFIG,
  type SemanticSearchConfig,
  type SearchableItem,
  type SearchResult,
  type ParsedQuery,
} from './semantic-search.js'

// Multi-Mode Tool
export {
  MultiModeDispatcher,
  createMultiModeDispatcher,
  defineMode,
  executeMode,
  createModeRouter,
  validateMode,
  getModeEnum,
  type ModeDefinition,
  type MultiModeToolConfig,
  type ModeDispatchResult,
} from './multi-mode-tool.js'

// Idle Maintenance
export {
  IdleMaintenanceManager,
  getIdleMaintenanceManager,
  clearIdleMaintenanceManager,
  registerCommonTasks,
  MAINTENANCE_PRIORITY,
  type MaintenanceTask,
  type IdleMaintenanceConfig,
  type MaintenanceResult,
} from './idle-maintenance.js'

// Compliance Hooks
export {
  checkCompliance,
  getComplianceReminder,
  checkAndTrack,
  registerRule,
  unregisterRule,
  enableRule,
  disableRule,
  getRules,
  clearRules,
  registerDefaultRules,
  trackToolExecution,
  getRecentTools,
  clearToolHistory,
  clearAllToolHistory,
  createComplianceContext,
  isCodeReadTool,
  isCodeModifyTool,
  isDelegationTool,
  isValidationTool,
  isTaskCompletionTool,
  type AgentRole,
  type ToolContext,
  type ComplianceCheckResult,
  type ComplianceRule,
} from './compliance-hooks.js'
