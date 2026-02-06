/**
 * Extension System Types
 * Type definitions for the plugin/extension architecture.
 *
 * Extensions bundle capabilities: MCP servers, context files, excluded tools.
 * They can be installed from local paths, git repos, or symlinked for development.
 */

// ============================================================================
// Extension Config (on-disk format: estela-extension.json)
// ============================================================================

/**
 * Extension configuration as stored in `estela-extension.json`.
 * This is the source-of-truth schema for what an extension declares.
 */
export interface ExtensionConfig {
  /** Unique extension name (alphanumeric + dashes) */
  name: string
  /** Semantic version */
  version: string
  /** Human-readable description */
  description?: string
  /** MCP servers provided by this extension */
  mcpServers?: Record<string, MCPServerExtConfig>
  /** Context file names to load (default: ["ESTELA.md"]) */
  contextFiles?: string | string[]
  /** Tool names to exclude when this extension is active */
  excludeTools?: string[]
}

/**
 * MCP server configuration within an extension
 */
export interface MCPServerExtConfig {
  type: 'stdio' | 'sse' | 'http'
  command?: string
  url?: string
  args?: string[]
  env?: Record<string, string>
  cwd?: string
  timeout?: number
}

// ============================================================================
// Install Metadata (on-disk format: .estela-extension-install.json)
// ============================================================================

/** How the extension was installed */
export type InstallType = 'local' | 'link' | 'git'

/**
 * Metadata about how an extension was installed.
 * Stored alongside the extension in `.estela-extension-install.json`.
 */
export interface InstallMetadata {
  /** Installation method */
  type: InstallType
  /** Source path or URL */
  source: string
  /** ISO 8601 timestamp of installation */
  installedAt: string
  /** Git ref (branch/tag/commit) for git installs */
  ref?: string
}

// ============================================================================
// Loaded Extension (runtime representation)
// ============================================================================

/**
 * A fully loaded extension with resolved paths and state.
 */
export interface Extension {
  /** Extension name from config */
  name: string
  /** Extension version from config */
  version: string
  /** Description from config */
  description?: string
  /** Absolute path to extension directory */
  path: string
  /** Resolved absolute paths to context files that exist */
  contextFiles: string[]
  /** MCP server configurations */
  mcpServers?: Record<string, MCPServerExtConfig>
  /** Tools to exclude */
  excludeTools?: string[]
  /** Installation metadata (undefined for manually placed extensions) */
  installMetadata?: InstallMetadata
  /** Whether this extension is currently active */
  isActive: boolean
}

// ============================================================================
// Extension Events
// ============================================================================

/** Extension lifecycle events */
export type ExtensionEvent =
  | { type: 'installed'; extension: Extension }
  | { type: 'uninstalled'; name: string }
  | { type: 'enabled'; name: string }
  | { type: 'disabled'; name: string }
  | { type: 'loaded'; extensions: Extension[] }
  | { type: 'error'; name: string; error: string }

/** Listener for extension events */
export type ExtensionEventListener = (event: ExtensionEvent) => void

// ============================================================================
// Manager Options
// ============================================================================

/**
 * Options for creating an ExtensionManager
 */
export interface ExtensionManagerOptions {
  /** Working directory (for workspace-scoped enablement) */
  workspaceDir?: string
  /** Custom extensions directory (default: ~/.estela/extensions/) */
  extensionsDir?: string
  /** Custom enablement storage path */
  enablementPath?: string
}

/**
 * Options for installing an extension
 */
export interface InstallOptions {
  /** Git ref to checkout (branch/tag/commit) */
  ref?: string
  /** Install as symlink (for development) */
  link?: boolean
}

// ============================================================================
// Enablement (on-disk format: extension-enablement.json)
// ============================================================================

/**
 * Enablement state stored on disk
 */
export interface EnablementData {
  version: 1
  /** Map of extension name to enabled state */
  extensions: Record<string, boolean>
}
