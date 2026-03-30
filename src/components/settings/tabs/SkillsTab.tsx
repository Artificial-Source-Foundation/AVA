/**
 * Rules & Commands Tab — Pencil design revamp
 *
 * Two cards on one page:
 * 1. Rules — active rule files with status badges
 * 2. Custom Commands — TOML-based prompt templates with /command display
 *
 * Skills have moved to the dedicated AI > Skills tab.
 */

import { Plus, ScrollText, Terminal } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, Show } from 'solid-js'
import {
  type CustomCommandFile,
  listCommands,
  saveCommand,
} from '../../../services/custom-commands'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import { CommandEditForm } from './commands/CommandEditForm'
import { RulesSection } from './rules-section'

export const SkillsTab: Component = () => {
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
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: SETTINGS_CARD_GAP }}>
      {/* Page title */}
      <h1
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
        }}
      >
        Rules & Commands
      </h1>

      {/* ===== Rules Card ===== */}
      <div
        style={{
          background: '#111114',
          border: '1px solid #ffffff08',
          'border-radius': '12px',
          padding: '20px',
          display: 'flex',
          'flex-direction': 'column',
          gap: '12px',
        }}
      >
        {/* Header */}
        <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
          <ScrollText size={16} style={{ color: '#C8C8CC' }} />
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '14px',
                'font-weight': '500',
                color: '#F5F5F7',
              }}
            >
              Rules
            </span>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#48484A',
              }}
            >
              Project-specific instructions loaded from .ava/rules/
            </span>
          </div>
        </div>

        {/* Rule file rows */}
        <RulesSection />
      </div>

      {/* ===== Commands Card ===== */}
      <div
        style={{
          background: '#111114',
          border: '1px solid #ffffff08',
          'border-radius': '12px',
          padding: '20px',
          display: 'flex',
          'flex-direction': 'column',
          gap: '16px',
        }}
      >
        {/* Header */}
        <div
          style={{
            display: 'flex',
            'align-items': 'center',
            'justify-content': 'space-between',
          }}
        >
          <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
            <Terminal size={16} style={{ color: '#C8C8CC' }} />
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '14px',
                  'font-weight': '500',
                  color: '#F5F5F7',
                }}
              >
                Custom Commands
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#48484A',
                }}
              >
                Reusable TOML-based prompt templates
              </span>
            </div>
          </div>
          <button
            type="button"
            onClick={() => setEditingCmd('new')}
            style={{
              display: 'flex',
              'align-items': 'center',
              gap: '6px',
              padding: '6px 12px',
              background: '#0A84FF',
              'border-radius': '8px',
              border: 'none',
              cursor: 'pointer',
            }}
          >
            <Plus size={12} style={{ color: '#FFFFFF' }} />
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                'font-weight': '500',
                color: '#FFFFFF',
              }}
            >
              New Command
            </span>
          </button>
        </div>

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
                <Terminal size={24} style={{ color: '#48484A', 'margin-bottom': '8px' }} />
                <span
                  style={{
                    'font-family': 'Geist, sans-serif',
                    'font-size': '12px',
                    color: '#48484A',
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
                    background: '#ffffff04',
                    border: '1px solid #ffffff0a',
                    'border-radius': '8px',
                    cursor: 'pointer',
                    transition: 'border-color 0.15s',
                  }}
                >
                  <Terminal size={14} style={{ color: '#48484A', 'flex-shrink': '0' }} />
                  <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
                    <span
                      style={{
                        'font-family': 'Geist, sans-serif',
                        'font-size': '13px',
                        'font-weight': '500',
                        color: '#F5F5F7',
                      }}
                    >
                      /{cmd.name}
                    </span>
                    <Show when={cmd.description}>
                      <span
                        style={{
                          'font-family': 'Geist, sans-serif',
                          'font-size': '11px',
                          color: '#48484A',
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
      </div>
    </div>
  )
}
