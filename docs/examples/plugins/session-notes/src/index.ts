/**
 * Session Notes Plugin
 *
 * Demonstrates: registerCommand(), storage API
 * Provides a /notes command to store and retrieve session notes.
 */

import type { Disposable, ExtensionAPI, SlashCommand } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  const notesCommand: SlashCommand = {
    name: 'notes',
    description: 'Store or view session notes. Usage: /notes [add <text> | list | clear]',

    async execute(args, _ctx) {
      const parts = args.trim().split(/\s+/)
      const action = parts[0] ?? 'list'

      if (action === 'add') {
        const note = parts.slice(1).join(' ')
        if (!note) return 'Usage: /notes add <your note text>'

        const existing = (await api.storage.get<string[]>('notes')) ?? []
        existing.push(note)
        await api.storage.set('notes', existing)
        return `Note added (${existing.length} total).`
      }

      if (action === 'clear') {
        await api.storage.delete('notes')
        return 'All notes cleared.'
      }

      // Default: list
      const notes = (await api.storage.get<string[]>('notes')) ?? []
      if (notes.length === 0) return 'No notes yet. Use /notes add <text> to create one.'
      return notes.map((n, i) => `${i + 1}. ${n}`).join('\n')
    },
  }

  const disposable = api.registerCommand(notesCommand)
  api.log.info('Session notes command registered')
  return disposable
}
