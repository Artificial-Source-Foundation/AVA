/**
 * Slash Commands Module
 *
 * User-invocable commands in chat
 */

// Built-in commands
export {
  actCommand,
  builtinCommands,
  clearCommand,
  commandCategories,
  compactCommand,
  doneCommand,
  helpCommand,
  listTasksCommand,
  newTaskCommand,
  planCommand,
  subagentCommand,
} from './commands/index.js'

// Registry
export {
  CommandRegistry,
  createCommandRegistry,
  getCommandRegistry,
} from './registry.js'
// Types
export type {
  CategorizedCommand,
  CommandCategory,
  CommandContext,
  CommandEvent,
  CommandEventListener,
  CommandResult,
  SlashCommand,
} from './types.js'

// ============================================================================
// Initialization
// ============================================================================

import { builtinCommands, commandCategories } from './commands/index.js'
import { getCommandRegistry } from './registry.js'

/**
 * Initialize the command registry with built-in commands
 */
export function initializeCommands(): void {
  const registry = getCommandRegistry()

  for (const command of builtinCommands) {
    const category = commandCategories[command.name] || 'custom'
    registry.register(command, category)
  }
}

/**
 * Check if a message is a slash command
 */
export function isSlashCommand(message: string): boolean {
  return message.trim().startsWith('/')
}

/**
 * Execute a slash command from a message
 * Returns null if not a command
 */
export async function executeCommand(
  message: string,
  context: Omit<import('./types.js').CommandContext, 'args' | 'rawArgs'>
): Promise<import('./types.js').CommandResult | null> {
  const registry = getCommandRegistry()
  return registry.tryExecute(message, context)
}
