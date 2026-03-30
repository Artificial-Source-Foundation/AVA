/**
 * Permissions & Trust Tab — Pencil macOS-inspired flat design.
 *
 * Global mode (segmented control), tool rules, trusted folders.
 */

import { type Component, For } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { PermissionMode } from '../../../stores/settings/settings-types'
import { ToolRulesSection } from './permissions/ToolRulesSection'
import { TrustedFoldersTab } from './TrustedFoldersTab'

/** Shared divider */
const Divider: Component = () => (
  <div style={{ width: '100%', height: '1px', background: '#ffffff06' }} />
)

export const PermissionsAndTrustTab: Component = () => {
  const { settings, updateSettings } = useSettings()

  const rules = () => settings().toolRules

  const modes: PermissionMode[] = ['ask', 'auto-approve']
  const modeLabels: Record<PermissionMode, string> = {
    ask: 'Ask',
    'auto-approve': 'Auto',
    bypass: 'Auto',
  }

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '32px' }}>
      {/* Page title */}
      <h1
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
        }}
      >
        Permissions & Trust
      </h1>

      {/* Global Mode Section */}
      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '12px' }}>
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '14px',
            'font-weight': '500',
            color: '#F5F5F7',
          }}
        >
          Global Mode
        </span>
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '12px',
            color: '#48484A',
          }}
        >
          Control how AVA asks for permission before running tools
        </span>

        {/* Segmented control */}
        <div
          class="flex items-center"
          style={{
            'border-radius': '8px',
            background: '#111114',
            border: '1px solid #ffffff0a',
            padding: '3px',
            gap: '2px',
            width: 'fit-content',
          }}
        >
          <For each={modes}>
            {(mode) => {
              const isActive = () => settings().permissionMode === mode
              return (
                <button
                  type="button"
                  onClick={() => updateSettings({ permissionMode: mode })}
                  class="flex items-center justify-center"
                  style={{
                    'border-radius': '6px',
                    height: '28px',
                    padding: '0 20px',
                    background: isActive() ? '#0A84FF' : 'transparent',
                    color: isActive() ? '#FFFFFF' : '#48484A',
                    'font-family': 'Geist, sans-serif',
                    'font-size': '13px',
                    'font-weight': isActive() ? '500' : '400',
                    border: 'none',
                    cursor: 'pointer',
                    transition: 'background 150ms, color 150ms',
                  }}
                >
                  {modeLabels[mode]}
                </button>
              )
            }}
          </For>
        </div>
      </div>

      <Divider />

      {/* Tool Rules Section */}
      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '14px' }}>
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '14px',
            'font-weight': '500',
            color: '#F5F5F7',
          }}
        >
          Tool Rules
        </span>
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '12px',
            color: '#48484A',
          }}
        >
          Override the global mode for specific tools or patterns
        </span>
        <ToolRulesSection rules={rules()} onUpdateRules={(r) => updateSettings({ toolRules: r })} />
      </div>

      <Divider />

      {/* Trusted Folders Section */}
      <TrustedFoldersTab />
    </div>
  )
}
