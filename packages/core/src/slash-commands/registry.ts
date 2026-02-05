/**
 * Slash Command Registry
 *
 * Manages registration and execution of slash commands
 */

import type {
  CategorizedCommand,
  CommandCategory,
  CommandContext,
  CommandEvent,
  CommandEventListener,
  CommandResult,
  SlashCommand,
} from './types.js'

// ============================================================================
// Registry Class
// ============================================================================

/**
 * Slash Command Registry
 *
 * Registers and executes user-invocable commands
 */
export class CommandRegistry {
  private commands: Map<string, CategorizedCommand> = new Map()
  private aliases: Map<string, string> = new Map()
  private listeners: Set<CommandEventListener> = new Set()

  // ==========================================================================
  // Registration
  // ==========================================================================

  /**
   * Register a command
   */
  register(command: SlashCommand, category: CommandCategory = 'custom'): void {
    const categorized: CategorizedCommand = { ...command, category }
    this.commands.set(command.name, categorized)

    // Register aliases
    if (command.aliases) {
      for (const alias of command.aliases) {
        this.aliases.set(alias, command.name)
      }
    }
  }

  /**
   * Unregister a command
   */
  unregister(name: string): boolean {
    const command = this.commands.get(name)
    if (!command) return false

    // Remove aliases
    if (command.aliases) {
      for (const alias of command.aliases) {
        this.aliases.delete(alias)
      }
    }

    this.commands.delete(name)
    return true
  }

  /**
   * Check if a command is registered
   */
  has(name: string): boolean {
    return this.commands.has(name) || this.aliases.has(name)
  }

  /**
   * Get a command by name or alias
   */
  get(name: string): CategorizedCommand | undefined {
    const command = this.commands.get(name)
    if (command) return command

    // Check aliases
    const aliasTarget = this.aliases.get(name)
    if (aliasTarget) {
      return this.commands.get(aliasTarget)
    }

    return undefined
  }

  /**
   * Get all registered commands
   */
  getAll(): CategorizedCommand[] {
    return Array.from(this.commands.values())
  }

  /**
   * Get commands by category
   */
  getByCategory(category: CommandCategory): CategorizedCommand[] {
    return this.getAll().filter((cmd) => cmd.category === category)
  }

  // ==========================================================================
  // Execution
  // ==========================================================================

  /**
   * Parse a message for slash commands
   * Returns the command and args if found, null otherwise
   */
  parse(message: string): { command: string; args: string[]; rawArgs: string } | null {
    const trimmed = message.trim()
    if (!trimmed.startsWith('/')) return null

    // Split command and args
    const firstSpace = trimmed.indexOf(' ')
    let commandName: string
    let rawArgs: string

    if (firstSpace === -1) {
      commandName = trimmed.slice(1)
      rawArgs = ''
    } else {
      commandName = trimmed.slice(1, firstSpace)
      rawArgs = trimmed.slice(firstSpace + 1).trim()
    }

    // Parse args (simple space-separated, respecting quotes)
    const args = this.parseArgs(rawArgs)

    return { command: commandName.toLowerCase(), args, rawArgs }
  }

  /**
   * Execute a command
   */
  async execute(commandName: string, context: CommandContext): Promise<CommandResult> {
    const command = this.get(commandName)

    if (!command) {
      this.emit({ type: 'command:unknown', command: commandName })
      return {
        success: false,
        message: `Unknown command: /${commandName}. Type /help for available commands.`,
      }
    }

    this.emit({ type: 'command:start', command: commandName, args: context.args })

    try {
      const result = await command.execute(context)
      this.emit({ type: 'command:success', command: commandName, result })
      return result
    } catch (error) {
      const err = error instanceof Error ? error : new Error(String(error))
      this.emit({ type: 'command:error', command: commandName, error: err })
      return {
        success: false,
        message: `Command failed: ${err.message}`,
      }
    }
  }

  /**
   * Check if a message is a command and execute it
   * Returns null if not a command
   */
  async tryExecute(
    message: string,
    context: Omit<CommandContext, 'args' | 'rawArgs'>
  ): Promise<CommandResult | null> {
    const parsed = this.parse(message)
    if (!parsed) return null

    const fullContext: CommandContext = {
      ...context,
      args: parsed.args,
      rawArgs: parsed.rawArgs,
    }

    return this.execute(parsed.command, fullContext)
  }

  // ==========================================================================
  // Events
  // ==========================================================================

  /**
   * Subscribe to command events
   */
  on(listener: CommandEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private emit(event: CommandEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch (error) {
        console.error('Command event listener error:', error)
      }
    }
  }

  // ==========================================================================
  // Help Generation
  // ==========================================================================

  /**
   * Generate help text for all commands
   */
  generateHelp(): string {
    const categories: CommandCategory[] = [
      'task',
      'context',
      'agent',
      'file',
      'git',
      'debug',
      'help',
      'custom',
    ]
    const lines: string[] = ['# Available Commands', '']

    for (const category of categories) {
      const commands = this.getByCategory(category).filter((cmd) => !cmd.hidden)
      if (commands.length === 0) continue

      lines.push(`## ${this.formatCategory(category)}`)
      lines.push('')

      for (const cmd of commands) {
        const aliases = cmd.aliases?.length
          ? ` (${cmd.aliases.map((a) => `/${a}`).join(', ')})`
          : ''
        lines.push(`- **/${cmd.name}**${aliases} - ${cmd.description}`)
        if (cmd.usage) {
          lines.push(`  Usage: \`${cmd.usage}\``)
        }
      }
      lines.push('')
    }

    return lines.join('\n')
  }

  /**
   * Generate help text for a specific command
   */
  generateCommandHelp(commandName: string): string | null {
    const command = this.get(commandName)
    if (!command) return null

    const lines: string[] = [`# /${command.name}`, '', command.description, '']

    if (command.aliases?.length) {
      lines.push(`**Aliases:** ${command.aliases.map((a) => `/${a}`).join(', ')}`)
      lines.push('')
    }

    if (command.usage) {
      lines.push(`**Usage:** \`${command.usage}\``)
      lines.push('')
    }

    if (command.examples?.length) {
      lines.push('**Examples:**')
      for (const example of command.examples) {
        lines.push(`- \`${example}\``)
      }
      lines.push('')
    }

    return lines.join('\n')
  }

  // ==========================================================================
  // Private Helpers
  // ==========================================================================

  /**
   * Parse arguments respecting quotes
   */
  private parseArgs(rawArgs: string): string[] {
    if (!rawArgs) return []

    const args: string[] = []
    let current = ''
    let inQuote = false
    let quoteChar = ''

    for (let i = 0; i < rawArgs.length; i++) {
      const char = rawArgs[i]

      if ((char === '"' || char === "'") && !inQuote) {
        inQuote = true
        quoteChar = char
      } else if (char === quoteChar && inQuote) {
        inQuote = false
        quoteChar = ''
      } else if (char === ' ' && !inQuote) {
        if (current) {
          args.push(current)
          current = ''
        }
      } else {
        current += char
      }
    }

    if (current) {
      args.push(current)
    }

    return args
  }

  /**
   * Format category name for display
   */
  private formatCategory(category: CommandCategory): string {
    const names: Record<CommandCategory, string> = {
      task: 'Task Management',
      context: 'Context Management',
      agent: 'Agent Control',
      file: 'File Operations',
      git: 'Git Operations',
      debug: 'Debugging',
      help: 'Help & Info',
      custom: 'Custom Commands',
    }
    return names[category]
  }
}

// ============================================================================
// Singleton Instance
// ============================================================================

let instance: CommandRegistry | null = null

/**
 * Get the command registry singleton
 */
export function getCommandRegistry(): CommandRegistry {
  if (!instance) {
    instance = new CommandRegistry()
  }
  return instance
}

/**
 * Create a new command registry
 */
export function createCommandRegistry(): CommandRegistry {
  return new CommandRegistry()
}
