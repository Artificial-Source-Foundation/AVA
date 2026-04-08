import { Bot, Code2, Info, Puzzle, Server } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { useSettings } from '../../../stores/settings'
import { AboutSection } from '../settings-about-section'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import type { SettingsTab } from '../settings-modal-config'
import { AgentsTab } from './AgentsTab'
import { DeveloperTab } from './DeveloperTab'

function AdvancedSectionHeader(props: { icon: typeof Bot; title: string; description: string }) {
  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
      <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
        <Dynamic component={props.icon} size={16} style={{ color: '#C8C8CC' }} />
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '14px',
            'font-weight': '600',
            color: '#F5F5F7',
          }}
        >
          {props.title}
        </span>
      </div>
      <p
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '12px',
          color: '#86868B',
          margin: '0',
        }}
      >
        {props.description}
      </p>
    </div>
  )
}

export const AdvancedTab: Component<{ onSelectTab?: (tab: SettingsTab) => void }> = (props) => {
  const { settings } = useSettings()

  const navButtonStyle = {
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
  } satisfies Record<string, string>

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: SETTINGS_CARD_GAP }}>
      <h1
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
          margin: '0',
        }}
      >
        Advanced
      </h1>

      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '12px' }}>
        <AdvancedSectionHeader
          icon={Info}
          title="About"
          description="Version details, runtime information, and project links."
        />
        <AboutSection />
      </div>

      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '12px' }}>
        <AdvancedSectionHeader
          icon={Bot}
          title="Agents"
          description="Manage built-in and custom agent presets used by the desktop app."
        />
        <div style={{ 'min-height': '420px', 'max-height': '720px', overflow: 'hidden' }}>
          <AgentsTab />
        </div>
      </div>

      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '12px' }}>
        <AdvancedSectionHeader
          icon={Puzzle}
          title="Extension Surfaces"
          description="Plugins and MCP stay available, but they now live under Advanced by default."
        />
        <div
          style={{
            display: 'flex',
            gap: '12px',
            'flex-wrap': 'wrap',
            padding: '20px',
            background: '#111114',
            border: '1px solid #ffffff08',
            'border-radius': '12px',
          }}
        >
          <button
            type="button"
            onClick={() => props.onSelectTab?.('plugins')}
            class="flex items-center transition-colors"
            style={navButtonStyle}
          >
            <Puzzle size={14} />
            Plugins
          </button>
          <button
            type="button"
            onClick={() => props.onSelectTab?.('mcp')}
            class="flex items-center transition-colors"
            style={navButtonStyle}
          >
            <Server size={14} />
            MCP Servers
          </button>
        </div>
      </div>

      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '12px' }}>
        <AdvancedSectionHeader
          icon={Code2}
          title="Developer"
          description="Developer diagnostics, logging, and advanced debugging tools."
        />
        <Show
          when={settings().devMode}
          fallback={
            <div
              style={{
                background: '#111114',
                border: '1px solid #ffffff08',
                'border-radius': '12px',
                padding: '20px',
                color: '#86868B',
                'font-size': '13px',
              }}
            >
              Enable Developer Mode below to access live console output and debug controls.
            </div>
          }
        >
          <DeveloperTab />
        </Show>
      </div>
    </div>
  )
}
