/**
 * Rules & Commands Content
 *
 * Uses shared settings components and theme tokens for consistency.
 * Shared content block rendered inside the merged Skills settings surface.
 */

import { Plus, ScrollText, Terminal } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, Show } from 'solid-js'
import {
  type CustomCommandFile,
  listCommands,
  saveCommand,
} from '../../../services/custom-commands'
import {
  SettingsActionHeader,
  SettingsButton,
  SettingsCard,
  SettingsCardSimple,
  SettingsTabContainer,
} from '../shared-settings-components'
import { CommandEditForm } from './commands/CommandEditForm'
import { RulesSection } from './rules-section'

export const RulesAndCommandsContent: Component = () => {
  // ----- Commands state -----
  const [commands, setCommands] = createSignal<CustomCommandFile[]>([])
  const [editingCmd, setEditingCmd] = createSignal<null | 'new' | string>(null)
  const [cmdLoading, setCmdLoading] = createSignal(true)

  const loadCommands = async () => {
    setCmdLoading(true)
    try {
      const cmds = await listCommands()
      setCommands(cmds)
    } finally {
      setCmdLoading(false)
    }
  }

  createEffect(() => {
    loadCommands()
  })

  const editingCommand = () => {
    const e = editingCmd()
    if (!e || e === 'new') return undefined
    return commands().find((c) => c.filePath === e)
  }

  const handleSaveCmd = async (cmd: Omit<CustomCommandFile, 'filePath'>, existingPath?: string) => {
    await saveCommand(cmd, existingPath)
    setEditingCmd(null)
    await loadCommands()
  }

  return (
    <SettingsTabContainer>
      {/* ===== Rules Card ===== */}
      <SettingsCard
        icon={ScrollText}
        title="Rules"
        description="Project-specific instructions loaded from .ava/rules/"
        compact
      >
        <RulesSection />
      </SettingsCard>

      {/* ===== Commands Card ===== */}
      <SettingsCardSimple>
        <SettingsActionHeader
          icon={Terminal}
          title="Custom Commands"
          description="Reusable TOML-based prompt templates"
          action={
            <SettingsButton variant="primary" onClick={() => setEditingCmd('new')} icon={Plus}>
              New Command
            </SettingsButton>
          }
        />

        {/* Inline command form */}
        <Show when={editingCmd()}>
          <CommandEditForm
            initial={editingCommand()}
            onSave={handleSaveCmd}
            onCancel={() => setEditingCmd(null)}
          />
        </Show>

        {/* Command rows */}
        <Show when={!cmdLoading()}>
          <Show
            when={commands().length > 0}
            fallback={
              <div
                style={{
                  display: 'flex',
                  'flex-direction': 'column',
                  'align-items': 'center',
                  'justify-content': 'center',
                  padding: '32px 0',
                  'text-align': 'center',
                }}
              >
                <Terminal
                  size={24}
                  style={{ color: 'var(--text-muted)', 'margin-bottom': '8px' }}
                />
                <span
                  style={{
                    'font-family': 'var(--font-sans)',
                    'font-size': '12px',
                    color: 'var(--text-muted)',
                  }}
                >
                  No custom commands yet
                </span>
              </div>
            }
          >
            <For each={commands()}>
              {(cmd) => (
                // biome-ignore lint/a11y/useKeyWithClickEvents: command row selection
                // biome-ignore lint/a11y/useSemanticElements: card-style row
                <div
                  role="button"
                  tabIndex={0}
                  onClick={() => setEditingCmd(cmd.filePath)}
                  style={{
                    display: 'flex',
                    'align-items': 'center',
                    gap: '10px',
                    padding: '10px 14px',
                    background: 'var(--surface-sunken)',
                    border: '1px solid var(--border-subtle)',
                    'border-radius': '8px',
                    cursor: 'pointer',
                    transition: 'border-color 0.15s',
                  }}
                >
                  <Terminal size={14} style={{ color: 'var(--text-muted)', 'flex-shrink': '0' }} />
                  <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
                    <span
                      style={{
                        'font-family': 'var(--font-sans)',
                        'font-size': '13px',
                        'font-weight': '500',
                        color: 'var(--text-primary)',
                      }}
                    >
                      /{cmd.name}
                    </span>
                    <Show when={cmd.description}>
                      <span
                        style={{
                          'font-family': 'var(--font-sans)',
                          'font-size': '11px',
                          color: 'var(--text-muted)',
                        }}
                      >
                        {cmd.description}
                      </span>
                    </Show>
                  </div>
                </div>
              )}
            </For>
          </Show>
        </Show>
      </SettingsCardSimple>
    </SettingsTabContainer>
  )
}
