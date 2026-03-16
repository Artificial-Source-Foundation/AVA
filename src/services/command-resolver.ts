/**
 * Command Resolver
 *
 * Bridges the core-v2 slash command registry to the desktop app.
 * Parses user input for /commands, resolves them against the registry,
 * and provides a sorted list for autocomplete.
 */

/** Minimal SlashCommand type (replaces @ava/core-v2/extensions import) */
export interface SlashCommand {
  description: string
  execute?: (...args: unknown[]) => unknown
  [key: string]: unknown
}

/** Local command registry — populated by frontend code instead of core-v2 */
const _commands = new Map<string, SlashCommand>()

function getCommands(): Map<string, SlashCommand> {
  return _commands
}

/** Register a command in the local registry */
export function registerCommand(name: string, command: SlashCommand): void {
  _commands.set(name, command)
}

// Commands that execute locally in the UI (no agent turn needed)
const BUILT_IN_NAMES = new Set([
  'help',
  'clear',
  'mode',
  'architect',
  'model',
  'compact',
  'undo',
  'redo',
  'settings',
  'status',
  'export',
  'init',
])

export interface ParsedCommand {
  name: string
  args: string
}

export interface ResolvedCommand {
  name: string
  args: string
  command: SlashCommand
  isBuiltIn: boolean
}

export interface CommandEntry {
  name: string
  description: string
  isBuiltIn: boolean
}

/**
 * Parse a slash command from raw input.
 * Returns null if input doesn't match the /command pattern.
 * Avoids matching file paths like /home/user/file.
 */
export function parseSlashCommand(input: string): ParsedCommand | null {
  const match = input.match(/^\/([a-zA-Z][\w-]*)(?:\s+(.*))?$/)
  if (!match) return null
  return { name: match[1]!, args: match[2]?.trim() ?? '' }
}

/**
 * Resolve a parsed command against the registry.
 * Returns null if the command is not registered.
 */
export function resolveCommand(parsed: ParsedCommand): ResolvedCommand | null {
  const command = getCommands().get(parsed.name)
  if (!command) return null
  return {
    name: parsed.name,
    args: parsed.args,
    command,
    isBuiltIn: BUILT_IN_NAMES.has(parsed.name),
  }
}

/**
 * Get all available commands sorted by name, for autocomplete.
 */
export function getAvailableCommands(): CommandEntry[] {
  const entries: CommandEntry[] = []
  for (const [name, cmd] of getCommands()) {
    entries.push({ name, description: cmd.description, isBuiltIn: BUILT_IN_NAMES.has(name) })
  }
  return entries.sort((a, b) => a.name.localeCompare(b.name))
}
