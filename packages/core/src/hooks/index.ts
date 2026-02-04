/**
 * Hook System
 * Tool lifecycle hooks for customizing agent behavior
 *
 * @example
 * ```typescript
 * import { HookRunner, runHook, createPreToolUseContext } from './hooks/index.js'
 *
 * // Using the singleton runner
 * const result = await runHook('PreToolUse', createPreToolUseContext({
 *   toolName: 'write',
 *   parameters: { path: '/tmp/test.txt', content: 'hello' },
 *   workingDirectory: '/home/user/project',
 *   sessionId: 'session-123',
 * }))
 *
 * if (result.cancel) {
 *   console.log('Tool cancelled:', result.errorMessage)
 * }
 *
 * // Or create a custom runner
 * const runner = new HookRunner('/home/user/project', { timeout: 60000 })
 * await runner.initialize()
 *
 * runner.on((event) => {
 *   console.log(`Hook event: ${event.type} for ${event.hookType}`)
 * })
 * ```
 *
 * @module hooks
 */

// ============================================================================
// Types
// ============================================================================

export type {
  HookConfig,
  // Context types
  HookContext,
  HookContextMap,
  HookEvent,
  HookEventListener,
  // Events
  HookEventType,
  // Configuration
  HookLocation,
  // Result types
  HookResult,
  // Hook type enum
  HookType,
  PostToolUseContext,
  PreToolUseContext,
  TaskCancelContext,
  TaskCompleteContext,
  TaskStartContext,
} from './types.js'

export { DEFAULT_HOOK_CONFIG } from './types.js'

// ============================================================================
// Factory Functions
// ============================================================================

export {
  createCancelResult,
  // Result helpers
  createErrorResult,
  createPostToolUseContext,
  // Context creators
  createPreToolUseContext,
  createTaskCancelContext,
  createTaskCompleteContext,
  createTaskStartContext,
  mergeHookResults,
  // Output parsing
  parseHookOutput,
  serializeContext,
  validateHookResult,
} from './factory.js'

// ============================================================================
// Executor
// ============================================================================

export {
  discoverAllHooks,
  // Discovery
  discoverHooks,
  // Singleton management
  getHookRunner,
  // Runner class
  HookRunner,
  resetHookRunner,
  // Convenience function
  runHook,
} from './executor.js'
