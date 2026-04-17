import { Code2, Info } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { setDebugDevMode } from '../../../lib/debug-log'
import { useSettings } from '../../../stores/settings'
import { SettingsCard } from '../SettingsCard'
import { AboutSection } from '../settings-about-section'
import type { SettingsTab } from '../settings-modal-config'
import { DeveloperTab } from './DeveloperTab'

interface AdvancedTabProps {
  onSelectTab?: (tab: SettingsTab) => void
}

export const AdvancedTab: Component<AdvancedTabProps> = (props) => {
  const { settings, updateSettings } = useSettings()
  void props

  return (
    <div class="flex flex-col" style={{ gap: '24px' }}>
      {/* Page title */}
      <h2
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: 'var(--text-primary)',
          margin: '0',
        }}
      >
        Advanced
      </h2>

      {/* About Section */}
      <SettingsCard
        icon={Info}
        title="About"
        description="Version details, runtime information, and project links"
      >
        <AboutSection />
      </SettingsCard>

      {/* Developer Section */}
      <SettingsCard
        icon={Code2}
        title="Developer"
        description="Diagnostics, logging, and debugging tools"
      >
        <div
          style={{
            display: 'flex',
            'flex-direction': 'column',
            gap: '16px',
          }}
        >
          {/* Primary Developer Mode toggle - always visible for predictable focus */}
          <div
            style={{
              display: 'flex',
              'align-items': 'center',
              'justify-content': 'space-between',
            }}
          >
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '13px',
                  color: 'var(--text-secondary)',
                }}
              >
                Developer Mode
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: 'var(--text-muted)',
                }}
              >
                Enable to access live console output and debug controls
              </span>
            </div>
            <button
              type="button"
              role="switch"
              aria-checked={settings().devMode ?? false}
              onClick={() => {
                const next = !(settings().devMode ?? false)
                updateSettings({ devMode: next })
                setDebugDevMode(next)
              }}
              style={{
                width: '44px',
                height: '24px',
                'border-radius': '12px',
                background: settings().devMode ? 'var(--accent)' : 'var(--surface-overlay)',
                border: 'none',
                cursor: 'pointer',
                position: 'relative',
                'flex-shrink': '0',
                transition: 'background 0.15s',
              }}
              aria-label="Developer mode"
            >
              <span
                style={{
                  position: 'absolute',
                  width: '20px',
                  height: '20px',
                  'border-radius': '50%',
                  background: '#FFFFFF',
                  top: '2px',
                  left: settings().devMode ? '22px' : '2px',
                  transition: 'left 0.15s',
                }}
              />
            </button>
          </div>

          {/* Developer tools - shown only when enabled (toggle hidden since AdvancedTab provides it) */}
          <Show when={settings().devMode}>
            <DeveloperTab showToggle={false} />
          </Show>
        </div>
      </SettingsCard>
    </div>
  )
}
