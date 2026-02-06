/**
 * Slash Command Types
 *
 * User-invocable commands in chat
 */

// ============================================================================
// Core Types
// ============================================================================

/**
 * Context passed to command handlers
 */
export interface CommandContext {
  /** Current workspace root */
  workspaceRoot: string
  /** Current session ID */
  sessionId: string
  /** Arguments passed to the command */
  args: string[]
  /** Raw argument string */
  rawArgs: string
  /** Send a message to the user */
  sendMessage: (content: string) => Promise<void>
  /** Get platform */
  getPlatform: () => import('../platform.js').IPlatformProvider
}

/**
 * Result of executing a command
 */
export interface CommandResult {
  /** Whether the command succeeded */
  success: boolean
  /** Message to display to user */
  message?: string
  /** Data to inject into context */
  contextData?: string
  /** Whether to stop message processing */
  stopProcessing?: boolean
}

/**
 * A slash command definition
 */
export interface SlashCommand {
  /** Command name (without slash) */
  name: string
  /** Aliases for the command */
  aliases?: string[]
  /** Short description */
  description: string
  /** Detailed usage information */
  usage?: string
  /** Example invocations */
  examples?: string[]
  /** Whether command is hidden from help */
  hidden?: boolean
  /** Execute the command */
  execute: (context: CommandContext) => Promise<CommandResult>
}

/**
 * Command category for organization
 */
export type CommandCategory =
  | 'task' // Task management
  | 'context' // Context management
  | 'agent' // Agent control
  | 'file' // File operations
  | 'git' // Git operations
  | 'debug' // Debugging
  | 'help' // Help/info
  | 'custom' // User-defined

/**
 * Categorized command for help display
 */
export interface CategorizedCommand extends SlashCommand {
  category: CommandCategory
}

// ============================================================================
// Events
// ============================================================================

/**
 * Events emitted by command execution
 */
export type CommandEvent =
  | { type: 'command:start'; command: string; args: string[] }
  | { type: 'command:success'; command: string; result: CommandResult }
  | { type: 'command:error'; command: string; error: Error }
  | { type: 'command:unknown'; command: string }

/**
 * Event listener for command events
 */
export type CommandEventListener = (event: CommandEvent) => void
