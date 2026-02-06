/**
 * Custom Command Types
 * Type definitions for TOML-based custom commands
 */

// ============================================================================
// TOML Command Definition
// ============================================================================

/**
 * Parsed TOML command definition
 */
export interface CustomCommandDef {
  /** Command name (derived from filename) */
  name: string
  /** Short description for help menu */
  description?: string
  /** Prompt template with placeholders */
  prompt: string
  /** Source file path */
  sourcePath: string
  /** Whether this is a project-level command (higher priority) */
  isProjectLevel: boolean
}

// ============================================================================
// Template Types
// ============================================================================

/**
 * Placeholder types found in prompt templates
 */
export type PlaceholderType =
  | 'args' // {{args}} - argument injection
  | 'file' // @{path} - file content injection
  | 'shell' // !{command} - shell command injection

/**
 * A parsed placeholder from a template
 */
export interface Placeholder {
  /** Full matched string (e.g., "{{args}}", "@{file.md}", "!{git diff}") */
  raw: string
  /** Placeholder type */
  type: PlaceholderType
  /** Content inside the placeholder (e.g., "args", "file.md", "git diff") */
  content: string
  /** Start index in template */
  start: number
  /** End index in template */
  end: number
}

// ============================================================================
// Execution Types
// ============================================================================

/**
 * Result of template resolution
 */
export interface TemplateResult {
  /** Fully resolved prompt text */
  prompt: string
  /** Shell commands that were executed */
  shellCommands: ShellExecution[]
  /** Files that were injected */
  injectedFiles: string[]
  /** Whether any shell commands failed */
  hasErrors: boolean
}

/**
 * Record of a shell command execution during template resolution
 */
export interface ShellExecution {
  /** Original command from template */
  command: string
  /** Command output (stdout) */
  output: string
  /** Error output (stderr) */
  stderr?: string
  /** Exit code */
  exitCode: number
  /** Whether command succeeded */
  success: boolean
}

// ============================================================================
// Discovery Types
// ============================================================================

/**
 * Discovered command file info
 */
export interface CommandFileInfo {
  /** Full path to the TOML file */
  filePath: string
  /** Command name (derived from path) */
  name: string
  /** Whether this is a project-level command */
  isProjectLevel: boolean
}

/**
 * Discovery locations for custom commands
 */
export interface CommandDiscoveryConfig {
  /** Project-level commands directory (e.g., <project>/.estela/commands/) */
  projectDir?: string
  /** User-level commands directory (e.g., ~/.estela/commands/) */
  userDir?: string
  /** Additional custom directories */
  extraDirs?: string[]
}
