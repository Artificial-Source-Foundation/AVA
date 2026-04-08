import { Bot, Code2, Info } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { useSettings } from '../../../stores/settings'
import { AboutSection } from '../settings-about-section'
import { SETTINGS_CARD_GAP } from '../settings-constants'
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

export const AdvancedTab: Component = () => {
  const { settings } = useSettings()

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
