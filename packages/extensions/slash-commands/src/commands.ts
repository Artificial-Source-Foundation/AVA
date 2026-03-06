/**
 * Built-in slash commands.
 *
 * Each command emits events to stay decoupled from implementation details.
 * The agent loop or other extensions listen and act on these events.
 */

import type { SlashCommand } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'
import { getPlatform } from '@ava/core-v2/platform'
import { exportSessionToMarkdown } from '@ava/core-v2/session'
import type { ToolContext } from '@ava/core-v2/tools'

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

    cmd('architect', 'Toggle architect mode (e.g. /architect off)', async (args) => {
      const value = args.trim().toLowerCase()
      if (value === 'off') {
        emit('mode:switch', { mode: 'normal' })
        return 'Architect mode disabled.'
      }
      if (value && value !== 'on') {
        return 'Usage: /architect [on|off]'
      }
      emit('mode:switch', { mode: 'architect' })
      return 'Architect mode enabled.'
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

    cmd('redo', 'Redo the last undone file change', async (_args, ctx) => {
      emit('diff:redo', { sessionId: ctx.sessionId })
      return 'Redoing last undone change.'
    }),

    cmd('settings', 'Open settings', async () => {
      emit('ui:open-settings', {})
      return 'Opening settings.'
    }),

    cmd('status', 'Show current session status', async (_args, ctx) => {
      emit('session:status-requested', { sessionId: ctx.sessionId })
      return 'Fetching session status.'
    }),

    cmd('export', 'Export session conversation to markdown', async (_args, ctx) => {
      emit('session:export-requested', { sessionId: ctx.sessionId })
      // Retrieve messages from the event data (listeners will provide them)
      const messages: ChatMessage[] = []
      const md = exportSessionToMarkdown(messages)
      emit('session:exported', { sessionId: ctx.sessionId, format: 'markdown', content: md })
      return `Session exported to markdown (${md.length} chars).`
    }),

    cmd(
      'init',
      'Generate CLAUDE.md project instructions from detected config',
      async (_args, ctx) => {
        const cwd = ctx.workingDirectory
        const platform = getPlatform()
        const fs = platform.fs

        // Lazy-import to avoid circular dependency at module load time
        const { generateProjectRules } = await import(
          /* webpackIgnore: true */ '../../instructions/src/init.js'
        )
        const content = await generateProjectRules(cwd, fs)
        const outputPath = cwd.endsWith('/') ? `${cwd}CLAUDE.md` : `${cwd}/CLAUDE.md`

        const exists = await fs.exists(outputPath)
        if (exists) {
          emit('init:skipped', { path: outputPath, reason: 'CLAUDE.md already exists' })
          return `CLAUDE.md already exists at ${outputPath}. Delete it first to regenerate.`
        }

        await fs.writeFile(outputPath, content)
        emit('init:completed', { path: outputPath })
        return `Generated CLAUDE.md at ${outputPath} (${content.length} chars).`
      }
    ),
  ]
}
