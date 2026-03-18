/**
 * Commands Settings Tab
 *
 * Lists discovered custom commands from ~/.config/ava/commands/,
 * lets users create, edit, and delete TOML command files.
 */

import { ChevronDown, ChevronRight, FolderOpen, Pencil, Plus, Terminal, Trash2 } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, Show } from 'solid-js'
import {
  type CustomCommandFile,
  deleteCommand,
  listCommands,
  saveCommand,
} from '../../../services/custom-commands'
import { CommandEditForm } from './commands/CommandEditForm'

// ============================================================================
// Shared helpers
// ============================================================================

const SectionHeader: Component<{ title: string; right?: () => import('solid-js').JSX.Element }> = (
  props
) => (
  <div class="flex items-center justify-between mb-3">
    <h3 class="text-sm font-semibold text-[var(--text-primary)]">{props.title}</h3>
    {props.right?.()}
  </div>
)

// ============================================================================
// Command Card
// ============================================================================

interface CommandCardProps {
  command: CustomCommandFile
  onEdit: () => void
  onDelete: () => void
}

const CommandCard: Component<CommandCardProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  return (
    <div class="border border-[var(--border-subtle)] rounded-[var(--radius-lg)] bg-[var(--surface-raised)] overflow-hidden">
      <div class="flex items-center gap-2.5 px-3 py-2.5">
        <Terminal class="w-4 h-4 text-[var(--text-muted)] shrink-0" />
        <div class="flex-1 min-w-0">
          <div class="text-xs font-semibold text-[var(--text-primary)]">/{props.command.name}</div>
          <Show when={props.command.description}>
            <div class="text-[10px] text-[var(--text-muted)] truncate">
              {props.command.description}
            </div>
          </Show>
        </div>
        <div class="flex items-center gap-1 shrink-0">
          <button
            type="button"
            onClick={props.onEdit}
            class="p-1 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors"
            title="Edit command"
          >
            <Pencil class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={props.onDelete}
            class="p-1 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[color-mix(in_srgb,var(--error)_10%,transparent)] transition-colors"
            title="Delete command"
          >
            <Trash2 class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={() => setExpanded(!expanded())}
            class="p-1 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
            title={expanded() ? 'Collapse' : 'Show prompt'}
          >
            <Show when={expanded()} fallback={<ChevronRight class="w-3.5 h-3.5" />}>
              <ChevronDown class="w-3.5 h-3.5" />
            </Show>
          </button>
        </div>
      </div>

      <Show when={expanded()}>
        <div class="px-3 pb-2.5 border-t border-[var(--border-subtle)]">
          <pre class="text-[10px] text-[var(--text-secondary)] font-mono whitespace-pre-wrap mt-2 max-h-32 overflow-y-auto">
            {props.command.prompt}
          </pre>
          <div class="text-[9px] text-[var(--text-muted)] mt-1.5 font-mono truncate">
            {props.command.filePath}
          </div>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// Main Tab
// ============================================================================

export const CommandsTab: Component = () => {
  const [commands, setCommands] = createSignal<CustomCommandFile[]>([])
  const [editing, setEditing] = createSignal<null | 'new' | string>(null)
  const [loading, setLoading] = createSignal(true)

  const loadCommands = async () => {
    setLoading(true)
    try {
      const cmds = await listCommands()
      setCommands(cmds)
    } finally {
      setLoading(false)
    }
  }

  createEffect(() => {
    loadCommands()
  })

  const editingCommand = () => {
    const e = editing()
    if (!e || e === 'new') return undefined
    return commands().find((c) => c.filePath === e)
  }

  const handleSave = async (cmd: Omit<CustomCommandFile, 'filePath'>, existingPath?: string) => {
    await saveCommand(cmd, existingPath)
    setEditing(null)
    await loadCommands()
  }

  const handleDelete = async (filePath: string) => {
    await deleteCommand(filePath)
    await loadCommands()
  }

  // Show edit form when editing
  if (editing() === 'new' || editing()) {
    // This is reactive — handled below in JSX
  }

  return (
    <div class="space-y-4">
      <Show
        when={!editing()}
        fallback={
          <div>
            <SectionHeader title={editing() === 'new' ? 'New Command' : 'Edit Command'} />
            <CommandEditForm
              initial={editingCommand()}
              onSave={handleSave}
              onCancel={() => setEditing(null)}
            />
          </div>
        }
      >
        <SectionHeader
          title="Custom Commands"
          right={() => (
            <button
              type="button"
              onClick={() => setEditing('new')}
              class="flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:bg-[var(--accent-hover)] transition-colors"
            >
              <Plus class="w-3 h-3" />
              New Command
            </button>
          )}
        />

        <Show
          when={!loading()}
          fallback={
            <div class="space-y-2">
              <div class="h-14 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] animate-pulse" />
              <div class="h-14 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] animate-pulse" />
            </div>
          }
        >
          <Show
            when={commands().length > 0}
            fallback={
              <div class="flex flex-col items-center justify-center py-10 text-center">
                <FolderOpen class="w-8 h-8 text-[var(--text-muted)] mb-2" />
                <p class="text-xs text-[var(--text-secondary)] mb-1">No custom commands</p>
                <p class="text-[10px] text-[var(--text-muted)] max-w-xs mb-3">
                  Commands are TOML files in ~/.ava/commands/ that define reusable prompts
                </p>
                <button
                  type="button"
                  onClick={() => setEditing('new')}
                  class="inline-flex items-center gap-1.5 px-3 py-1.5 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:brightness-110 transition-colors"
                >
                  Create Command
                </button>
              </div>
            }
          >
            <div class="space-y-2">
              <For each={commands()}>
                {(cmd) => (
                  <CommandCard
                    command={cmd}
                    onEdit={() => setEditing(cmd.filePath)}
                    onDelete={() => handleDelete(cmd.filePath)}
                  />
                )}
              </For>
            </div>
          </Show>
        </Show>
      </Show>
    </div>
  )
}
