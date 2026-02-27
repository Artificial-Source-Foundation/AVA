/**
 * Built-in slash commands.
 *
 * Each command emits events to stay decoupled from implementation details.
 * The agent loop or other extensions listen and act on these events.
 */

import type { SlashCommand, ToolContext } from '@ava/core-v2/extensions'

function cmd(
  name: string,
  description: string,
  handler: (args: string, ctx: ToolContext) => Promise<string>
): SlashCommand {
  return { name, description, execute: handler }
}

export function createBuiltinCommands(
  emit: (event: string, data: unknown) => void
): SlashCommand[] {
  return [
    cmd('help', 'Show available commands and usage', async () => {
      emit('commands:help-requested', {})
      return 'Displaying help information.'
    }),

    cmd('clear', 'Clear the current session context', async (_args, ctx) => {
      emit('session:clear', { sessionId: ctx.sessionId })
      return 'Session context cleared.'
    }),

    cmd('mode', 'Switch agent mode (e.g. /mode plan)', async (args) => {
      const mode = args.trim()
      if (!mode) return 'Usage: /mode <mode-name>  (e.g. plan, normal, minimal)'
      emit('mode:switch', { mode })
      return `Switching to ${mode} mode.`
    }),

    cmd('model', 'Switch the active model (e.g. /model claude-sonnet)', async (args) => {
      const model = args.trim()
      if (!model) return 'Usage: /model <model-id>'
      emit('model:switch', { model })
      return `Switching to model: ${model}`
    }),

    cmd('compact', 'Compact conversation to reduce token usage', async (_args, ctx) => {
      emit('context:compact', { sessionId: ctx.sessionId })
      return 'Compacting conversation context.'
    }),

    cmd('undo', 'Undo the last file change', async (_args, ctx) => {
      emit('diff:undo', { sessionId: ctx.sessionId })
      return 'Undoing last change.'
    }),

    cmd('settings', 'Open settings', async () => {
      emit('ui:open-settings', {})
      return 'Opening settings.'
    }),

    cmd('status', 'Show current session status', async (_args, ctx) => {
      emit('session:status-requested', { sessionId: ctx.sessionId })
      return 'Fetching session status.'
    }),
  ]
}
