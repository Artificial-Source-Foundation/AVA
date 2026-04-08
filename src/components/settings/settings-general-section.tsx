/**
 * General Settings Section
 *
 * Card-based layout matching BehaviorTab / AppearanceTab pattern.
 * 4 cards: Interface, Git, Automation (future), Data.
 * Each card: #111114 surface, #ffffff08 border, rounded-12, 20px padding, 16px gap.
 */

import { Database, Download, GitBranch, Monitor, Upload } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useSettings } from '../../stores/settings'
import { Toggle } from '../ui/Toggle'
import { ToggleRow } from '../ui/ToggleRow'
import { SettingsCard } from './SettingsCard'
import { BehaviorSettingsContent } from './tabs/BehaviorTab'
import { type Keybinding, KeybindingsTab } from './tabs/KeybindingsTab'

interface GeneralSectionProps {
  keybindings: Keybinding[]
  onEditKeybinding: (id: string) => void
  onResetKeybinding: (id: string) => void
  onResetAllKeybindings: () => void
}

export const GeneralSection: Component<GeneralSectionProps> = (props) => {
  const { settings, updateUI, updateGit, exportSettings, importSettings } = useSettings()

  return (
    <div class="flex flex-col" style={{ gap: '24px' }}>
      {/* Page title */}
      <h2
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
          margin: '0',
        }}
      >
        General
      </h2>

      {/* Interface Card */}
      <SettingsCard
        icon={Monitor}
        title="Interface"
        description="Panel visibility and layout preferences"
      >
        <ToggleRow
          label="Show memory panel"
          description="Display the memory panel in the sidebar"
          checked={!!settings().ui.showBottomPanel}
          onChange={() => updateUI({ showBottomPanel: !settings().ui.showBottomPanel })}
        />
        <ToggleRow
          label="Show activity panel"
          description="Display agent activity in the sidebar"
          checked={!!settings().ui.showAgentActivity}
          onChange={() => updateUI({ showAgentActivity: !settings().ui.showAgentActivity })}
        />
        <ToggleRow
          label="Compact layout"
          description="Reduce padding and spacing throughout the UI"
          checked={!!settings().ui.compactMessages}
          onChange={() => updateUI({ compactMessages: !settings().ui.compactMessages })}
        />
        <ToggleRow
          label="Show token count"
          description="Display token usage in the status bar"
          checked={!!settings().ui.showTokenCount}
          onChange={() => updateUI({ showTokenCount: !settings().ui.showTokenCount })}
        />
      </SettingsCard>

      {/* Git Card */}
      <SettingsCard icon={GitBranch} title="Git" description="Version control and commit behavior">
        <ToggleRow
          label="Enable git integration"
          description="Track file changes with git snapshots"
          checked={settings().git.enabled}
          onChange={(v) => updateGit({ enabled: v })}
        />
        <Show when={settings().git.enabled}>
          <div class="flex items-center justify-between" style={{ width: '100%' }}>
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '13px',
                  color: '#C8C8CC',
                }}
              >
                Auto-commit
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#48484A',
                }}
              >
                Automatically commit after each task
              </span>
            </div>
            <div class="flex items-center" style={{ gap: '12px' }}>
              <Show when={settings().git.autoCommit}>
                <div
                  class="flex items-center"
                  style={{
                    'border-radius': '8px',
                    background: '#111114',
                    border: '1px solid #ffffff0a',
                    height: '32px',
                    padding: '0 12px',
                  }}
                >
                  <input
                    type="text"
                    value={settings().git.commitPrefix}
                    onInput={(e) => updateGit({ commitPrefix: e.currentTarget.value })}
                    class="outline-none"
                    style={{
                      background: 'transparent',
                      'font-family': 'Geist Mono, monospace',
                      'font-size': '12px',
                      color: '#F5F5F7',
                      width: '60px',
                      border: 'none',
                    }}
                    placeholder="[ava]"
                  />
                </div>
              </Show>
              <Toggle
                checked={settings().git.autoCommit}
                onChange={(v) => updateGit({ autoCommit: v })}
              />
            </div>
          </div>
        </Show>
      </SettingsCard>

      {/* Data Card */}
      <SettingsCard
        icon={Database}
        title="Data"
        description="Import, export, and manage application data"
      >
        <div class="flex items-center" style={{ gap: '12px', width: '100%' }}>
          <button
            type="button"
            onClick={() => exportSettings()}
            class="flex items-center transition-colors"
            style={{
              gap: '6px',
              'border-radius': '8px',
              border: '1px solid #ffffff0a',
              height: '32px',
              padding: '0 14px',
              color: '#C8C8CC',
              'font-family': 'Geist, sans-serif',
              'font-size': '13px',
              background: 'transparent',
              cursor: 'pointer',
            }}
          >
            <Download style={{ width: '14px', height: '14px' }} />
            Export Data
          </button>
          <button
            type="button"
            onClick={() => importSettings()}
            class="flex items-center transition-colors"
            style={{
              gap: '6px',
              'border-radius': '8px',
              border: '1px solid #ffffff0a',
              height: '32px',
              padding: '0 14px',
              color: '#C8C8CC',
              'font-family': 'Geist, sans-serif',
              'font-size': '13px',
              background: 'transparent',
              cursor: 'pointer',
            }}
          >
            <Upload style={{ width: '14px', height: '14px' }} />
            Import Data
          </button>
        </div>
        <div class="flex items-center" style={{ gap: '8px' }}>
          <span
            style={{ 'font-family': 'Geist, sans-serif', 'font-size': '13px', color: '#48484A' }}
          >
            Version
          </span>
          <span
            style={{
              'font-family': 'Geist Mono, monospace',
              'font-size': '13px',
              color: '#C8C8CC',
            }}
          >
            AVA v3.x
          </span>
        </div>
      </SettingsCard>

      <BehaviorSettingsContent />

      <KeybindingsTab
        keybindings={props.keybindings}
        onEdit={props.onEditKeybinding}
        onReset={props.onResetKeybinding}
        onResetAll={props.onResetAllKeybindings}
      />
    </div>
  )
}
