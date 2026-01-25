/**
 * Plugin System Types
 *
 * Extensibility framework for Delta9.
 */

import { z } from 'zod'

// =============================================================================
// Plugin Metadata
// =============================================================================

export const pluginMetadataSchema = z.object({
  /** Unique plugin identifier */
  id: z.string().regex(/^[a-z0-9-]+$/),
  /** Display name */
  name: z.string(),
  /** Version (semver) */
  version: z.string().regex(/^\d+\.\d+\.\d+$/),
  /** Description */
  description: z.string().optional(),
  /** Author */
  author: z.string().optional(),
  /** Repository URL */
  repository: z.string().url().optional(),
  /** Required Delta9 version */
  delta9Version: z.string().optional(),
  /** Plugin dependencies */
  dependencies: z.array(z.string()).default([]),
  /** Tags for discovery */
  tags: z.array(z.string()).default([]),
})

export type PluginMetadata = z.infer<typeof pluginMetadataSchema>

// =============================================================================
// Plugin Capabilities
// =============================================================================

export interface PluginAgent {
  /** Agent identifier */
  id: string
  /** Agent name */
  name: string
  /** Model to use */
  model: string
  /** System prompt */
  systemPrompt: string
  /** Temperature */
  temperature?: number
  /** Tools available to agent */
  tools?: string[]
  /** Custom handler */
  handler?: (input: unknown) => Promise<unknown>
}

export interface PluginTool {
  /** Tool identifier */
  id: string
  /** Tool name */
  name: string
  /** Description */
  description: string
  /** Zod schema for parameters */
  parameters: z.ZodType
  /** Execution handler */
  execute: (params: unknown) => Promise<unknown>
}

export interface PluginHook {
  /** Hook event type */
  event: PluginHookEvent
  /** Priority (lower = earlier) */
  priority?: number
  /** Handler function */
  handler: (context: HookContext) => Promise<void | HookResult>
}

export type PluginHookEvent =
  | 'mission.beforeCreate'
  | 'mission.afterCreate'
  | 'mission.beforeStart'
  | 'mission.afterComplete'
  | 'mission.onFail'
  | 'task.beforeExecute'
  | 'task.afterExecute'
  | 'task.onFail'
  | 'council.beforeConvene'
  | 'council.afterConvene'
  | 'operator.beforeTask'
  | 'operator.afterTask'
  | 'validator.beforeValidate'
  | 'validator.afterValidate'

export interface HookContext {
  /** Event type */
  event: PluginHookEvent
  /** Associated mission ID */
  missionId?: string
  /** Associated task ID */
  taskId?: string
  /** Event data */
  data: Record<string, unknown>
  /** Plugin services */
  services: PluginServices
}

export interface HookResult {
  /** Whether to continue with other hooks */
  continue?: boolean
  /** Modified data to pass forward */
  data?: Record<string, unknown>
  /** Abort the operation */
  abort?: boolean
  /** Abort reason */
  abortReason?: string
}

// =============================================================================
// Plugin Services
// =============================================================================

export interface PluginServices {
  /** Logging */
  log: (level: 'debug' | 'info' | 'warn' | 'error', message: string, data?: unknown) => void
  /** Get configuration */
  getConfig: <T>(key: string) => T | undefined
  /** Set configuration */
  setConfig: <T>(key: string, value: T) => void
  /** Emit event */
  emit: (event: string, data: unknown) => void
  /** Subscribe to event */
  on: (event: string, handler: (data: unknown) => void) => () => void
  /** Get mission state */
  getMission: (id: string) => unknown
  /** Get plugin */
  getPlugin: (id: string) => Plugin | undefined
}

// =============================================================================
// Plugin Definition
// =============================================================================

export interface Plugin {
  /** Plugin metadata */
  metadata: PluginMetadata
  /** Custom agents */
  agents?: PluginAgent[]
  /** Custom tools */
  tools?: PluginTool[]
  /** Event hooks */
  hooks?: PluginHook[]
  /** Custom commands */
  commands?: PluginCommand[]
  /** Configuration schema */
  configSchema?: z.ZodType
  /** Default configuration */
  defaultConfig?: Record<string, unknown>
  /** Initialize plugin */
  initialize?: (services: PluginServices) => Promise<void>
  /** Cleanup plugin */
  cleanup?: () => Promise<void>
}

export interface PluginCommand {
  /** Command name */
  name: string
  /** Description */
  description: string
  /** Arguments schema */
  args?: z.ZodType
  /** Execute command */
  execute: (args: unknown, services: PluginServices) => Promise<unknown>
}

// =============================================================================
// Plugin State
// =============================================================================

export type PluginState =
  | 'unloaded'
  | 'loading'
  | 'loaded'
  | 'initializing'
  | 'active'
  | 'error'
  | 'disabled'

export interface LoadedPlugin {
  /** Plugin definition */
  plugin: Plugin
  /** Current state */
  state: PluginState
  /** Load time */
  loadedAt?: string
  /** Error if failed */
  error?: string
  /** Plugin configuration */
  config: Record<string, unknown>
}

// =============================================================================
// Plugin Events
// =============================================================================

export type PluginEventType =
  | 'plugin.loaded'
  | 'plugin.initialized'
  | 'plugin.error'
  | 'plugin.disabled'
  | 'plugin.unloaded'

export interface PluginEvent {
  type: PluginEventType
  timestamp: string
  pluginId: string
  data: Record<string, unknown>
}

// =============================================================================
// Plugin Discovery
// =============================================================================

export interface PluginSource {
  /** Source type */
  type: 'local' | 'npm' | 'url' | 'inline'
  /** Path or URL */
  path?: string
  /** Package name */
  package?: string
  /** Version */
  version?: string
  /** Inline plugin */
  plugin?: Plugin
}

export interface PluginDiscoveryResult {
  /** Found plugins */
  plugins: Array<{
    source: PluginSource
    metadata: PluginMetadata
    valid: boolean
    error?: string
  }>
  /** Discovery errors */
  errors: string[]
}

// =============================================================================
// Plugin Registry
// =============================================================================

export interface PluginRegistry {
  /** Registered plugins */
  plugins: Map<string, LoadedPlugin>
  /** Plugin order (load order) */
  order: string[]
  /** Hooks by event */
  hooksByEvent: Map<PluginHookEvent, Array<{ pluginId: string; hook: PluginHook }>>
  /** Tools by ID */
  toolsById: Map<string, { pluginId: string; tool: PluginTool }>
  /** Agents by ID */
  agentsById: Map<string, { pluginId: string; agent: PluginAgent }>
  /** Commands by name */
  commandsByName: Map<string, { pluginId: string; command: PluginCommand }>
}
