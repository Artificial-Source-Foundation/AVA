/**
 * Built-in Slash Commands
 *
 * Core commands that ship with Estela
 */

import { getFocusChainManager } from '../../focus-chain/index.js'
import type { CommandContext, CommandResult, SlashCommand } from '../types.js'

// ============================================================================
// Task Commands
// ============================================================================

/**
 * /newtask - Start a new task
 */
export const newTaskCommand: SlashCommand = {
  name: 'newtask',
  aliases: ['task', 'todo'],
  description: 'Add a new task to the focus chain',
  usage: '/newtask <task description>',
  examples: ['/newtask Implement user authentication', '/newtask Fix bug in login form'],
  execute: async (context: CommandContext): Promise<CommandResult> => {
    const { rawArgs } = context

    if (!rawArgs) {
      return {
        success: false,
        message: 'Please provide a task description: /newtask <description>',
      }
    }

    const manager = getFocusChainManager()

    // Initialize if needed
    if (!manager.getChain()) {
      await manager.init(context.workspaceRoot)
    }

    const task = await manager.addTask(rawArgs)

    return {
      success: true,
      message: `Added task: ${task.text}`,
      contextData: `New task added to focus chain: "${task.text}"`,
    }
  },
}

/**
 * /tasks - List all tasks
 */
export const listTasksCommand: SlashCommand = {
  name: 'tasks',
  aliases: ['progress', 'status'],
  description: 'Show current task progress',
  usage: '/tasks',
  execute: async (context: CommandContext): Promise<CommandResult> => {
    const manager = getFocusChainManager()

    // Initialize if needed
    if (!manager.getChain()) {
      await manager.init(context.workspaceRoot)
    }

    const tasks = manager.getTasks()
    const progress = manager.getProgress()

    if (tasks.length === 0) {
      return {
        success: true,
        message: 'No tasks in the focus chain. Use /newtask to add one.',
      }
    }

    const statusEmoji: Record<string, string> = {
      pending: '⬜',
      in_progress: '🔄',
      completed: '✅',
      blocked: '🚫',
    }

    const taskLines = tasks.map((t) => {
      const indent = '  '.repeat(t.level)
      const emoji = statusEmoji[t.status]
      return `${indent}${emoji} ${t.text}`
    })

    const summary = `Progress: ${progress.percentComplete}% (${progress.completed}/${progress.total} tasks)`

    return {
      success: true,
      message: `# Task Progress\n\n${taskLines.join('\n')}\n\n${summary}`,
      contextData: `Current focus chain has ${progress.total} tasks, ${progress.completed} completed.`,
    }
  },
}

/**
 * /done - Mark current task as complete
 */
export const doneCommand: SlashCommand = {
  name: 'done',
  aliases: ['complete', 'finish'],
  description: 'Mark the current task as complete',
  usage: '/done [task-id]',
  execute: async (context: CommandContext): Promise<CommandResult> => {
    const manager = getFocusChainManager()

    if (!manager.getChain()) {
      await manager.init(context.workspaceRoot)
    }

    // Get task to complete
    let taskId: string | undefined
    if (context.args.length > 0) {
      taskId = context.args[0]
    } else {
      // Complete the active or next task
      const activeTask = manager.getActiveTask()
      const nextTask = manager.getNextTask()
      taskId = activeTask?.id || nextTask?.id
    }

    if (!taskId) {
      return {
        success: false,
        message: 'No task to complete. Use /newtask to add one.',
      }
    }

    const task = await manager.completeTask(taskId)
    const progress = manager.getProgress()

    return {
      success: true,
      message: `✅ Completed: ${task.text}\n\nProgress: ${progress.percentComplete}% (${progress.completed}/${progress.total})`,
      contextData: `Task completed: "${task.text}"`,
    }
  },
}

// ============================================================================
// Context Commands
// ============================================================================

/**
 * /compact - Compact context (summarize and reduce)
 */
export const compactCommand: SlashCommand = {
  name: 'compact',
  aliases: ['summarize', 'reduce'],
  description: 'Compact the conversation context',
  usage: '/compact',
  execute: async (_context: CommandContext): Promise<CommandResult> => {
    // This will trigger context compaction in the agent
    return {
      success: true,
      message:
        'Context compaction requested. The conversation will be summarized to reduce token usage.',
      contextData: 'SYSTEM: Compact the current context by summarizing the conversation so far.',
      stopProcessing: false,
    }
  },
}

/**
 * /clear - Clear context (start fresh)
 */
export const clearCommand: SlashCommand = {
  name: 'clear',
  aliases: ['reset', 'new'],
  description: 'Clear conversation context and start fresh',
  usage: '/clear',
  execute: async (_context: CommandContext): Promise<CommandResult> => {
    return {
      success: true,
      message: 'Context cleared. Starting fresh conversation.',
      stopProcessing: true,
    }
  },
}

// ============================================================================
// Agent Commands
// ============================================================================

/**
 * /subagent - Spawn a subagent for a task
 */
export const subagentCommand: SlashCommand = {
  name: 'subagent',
  aliases: ['delegate', 'spawn'],
  description: 'Spawn a subagent to handle a specific task',
  usage: '/subagent <task description>',
  examples: ['/subagent Research React best practices', '/subagent Write tests for auth module'],
  execute: async (context: CommandContext): Promise<CommandResult> => {
    const { rawArgs } = context

    if (!rawArgs) {
      return {
        success: false,
        message: 'Please provide a task for the subagent: /subagent <task>',
      }
    }

    return {
      success: true,
      message: `Spawning subagent for: ${rawArgs}`,
      contextData: `SYSTEM: Spawn a subagent to handle this task: "${rawArgs}". Use the task tool with appropriate subagent type.`,
    }
  },
}

/**
 * /plan - Enter planning mode
 */
export const planCommand: SlashCommand = {
  name: 'plan',
  aliases: ['think', 'analyze'],
  description: 'Enter planning mode (read-only, think before acting)',
  usage: '/plan',
  execute: async (_context: CommandContext): Promise<CommandResult> => {
    return {
      success: true,
      message: 'Entering planning mode. I will analyze and plan before taking any actions.',
      contextData:
        'SYSTEM: Enter planning mode. Only use read-only tools (glob, grep, read, ls). Think through the approach before acting.',
    }
  },
}

/**
 * /act - Exit planning mode
 */
export const actCommand: SlashCommand = {
  name: 'act',
  aliases: ['execute', 'do'],
  description: 'Exit planning mode and start executing',
  usage: '/act',
  execute: async (_context: CommandContext): Promise<CommandResult> => {
    return {
      success: true,
      message: 'Exiting planning mode. Ready to execute.',
      contextData: 'SYSTEM: Exit planning mode. All tools are now available.',
    }
  },
}

// ============================================================================
// Help Commands
// ============================================================================

/**
 * /help - Show available commands
 */
export const helpCommand: SlashCommand = {
  name: 'help',
  aliases: ['?', 'commands'],
  description: 'Show available commands',
  usage: '/help [command]',
  execute: async (context: CommandContext): Promise<CommandResult> => {
    const { args } = context

    // Dynamic import to avoid circular dependency
    const { getCommandRegistry } = await import('../registry.js')
    const registry = getCommandRegistry()

    if (args.length > 0) {
      const commandHelp = registry.generateCommandHelp(args[0])
      if (commandHelp) {
        return { success: true, message: commandHelp }
      }
      return {
        success: false,
        message: `Unknown command: /${args[0]}`,
      }
    }

    return {
      success: true,
      message: registry.generateHelp(),
    }
  },
}

// ============================================================================
// Export All Commands
// ============================================================================

export const builtinCommands: SlashCommand[] = [
  // Task
  newTaskCommand,
  listTasksCommand,
  doneCommand,
  // Context
  compactCommand,
  clearCommand,
  // Agent
  subagentCommand,
  planCommand,
  actCommand,
  // Help
  helpCommand,
]

/**
 * Category mapping for built-in commands
 */
export const commandCategories: Record<string, import('../types.js').CommandCategory> = {
  newtask: 'task',
  tasks: 'task',
  done: 'task',
  compact: 'context',
  clear: 'context',
  subagent: 'agent',
  plan: 'agent',
  act: 'agent',
  help: 'help',
}
