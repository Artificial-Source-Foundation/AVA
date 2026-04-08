/**
 * Command Resolver
 *
 * Local slash-command registry for the desktop app.
 * Parses user input for /commands, resolves them against the registry,
 * and provides a sorted list for autocomplete.
 */

/** Minimal slash-command shape used by the frontend registry. */
export interface SlashCommand {
  description: string
  execute?: (...args: unknown[]) => unknown
  [key: string]: unknown
}

/** Local command registry populated by frontend code. */
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
  'later',
  'queue',
  'new',
  'sessions',
  'copy',
  'shortcuts',
  'theme',
  'permissions',
  'think',
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

/** Register built-in slash commands exposed in desktop chat */
export function registerBuiltInCommands(): void {
  // ── Local UI commands (handled in frontend) ──
  registerCommand('compact', {
    description: 'Compact conversation context and preserve a summary',
  })
  registerCommand('later', {
    description: 'Queue a post-complete message for after the agent finishes',
  })
  registerCommand('queue', {
    description: 'Show the current message queue',
  })
  registerCommand('clear', {
    description: 'Clear the current chat',
  })
  registerCommand('new', {
    description: 'Start a new session',
  })
  registerCommand('sessions', {
    description: 'Open the session picker',
  })
  registerCommand('model', {
    description: 'Show or switch the current model',
  })
  registerCommand('theme', {
    description: 'Cycle or switch theme',
  })
  registerCommand('permissions', {
    description: 'Open permissions settings',
  })
  registerCommand('think', {
    description: 'Toggle thinking visibility',
  })
  registerCommand('export', {
    description: 'Export the current conversation',
  })
  registerCommand('copy', {
    description: 'Copy the last assistant response',
  })
  registerCommand('help', {
    description: 'Show available commands and shortcuts',
  })
  registerCommand('shortcuts', {
    description: 'Show keyboard shortcuts',
  })
  registerCommand('settings', {
    description: 'Open settings',
  })

  // ── Agent-handled commands (sent to Rust backend) ──
  registerCommand('commit', {
    description: 'Inspect commit readiness',
  })
  registerCommand('connect', {
    description: 'Add provider credentials',
  })
  registerCommand('providers', {
    description: 'Show provider status',
  })
  registerCommand('disconnect', {
    description: 'Remove provider credentials',
  })
  registerCommand('mcp', {
    description: 'Manage MCP servers',
  })
  registerCommand('btw', {
    description: 'Start a side conversation branch',
  })
  registerCommand('hooks', {
    description: 'Manage lifecycle hooks',
  })
  registerCommand('tasks', {
    description: 'Show background tasks',
  })
  registerCommand('init', {
    description: 'Create project templates',
  })
  registerCommand('rewind', {
    description: 'Browse conversation checkpoint history',
  })
}
