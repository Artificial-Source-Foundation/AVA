/**
 * Trusted Folders Section
 *
 * Pencil macOS-inspired card list with shield icons.
 */

import { Plus, ShieldCheck, ShieldX, X } from 'lucide-solid'
import { type Component, createSignal, For } from 'solid-js'
import { useSettings } from '../../../stores/settings'

export const TrustedFoldersTab: Component = () => {
  const { settings, updateSettings } = useSettings()
  const [newAllowed, setNewAllowed] = createSignal('')
  const [newDenied, setNewDenied] = createSignal('')

  const addAllowed = () => {
    const path = newAllowed().trim()
    if (!path) return
    const current = settings().trustedFolders
    if (current.allowed.includes(path)) return
    updateSettings({ trustedFolders: { ...current, allowed: [...current.allowed, path] } })
    setNewAllowed('')
  }

  const addDenied = () => {
    const path = newDenied().trim()
    if (!path) return
    const current = settings().trustedFolders
    if (current.denied.includes(path)) return
    updateSettings({ trustedFolders: { ...current, denied: [...current.denied, path] } })
    setNewDenied('')
  }

  const removeAllowed = (path: string) => {
    const current = settings().trustedFolders
    updateSettings({
      trustedFolders: { ...current, allowed: current.allowed.filter((p) => p !== path) },
    })
  }

  const removeDenied = (path: string) => {
    const current = settings().trustedFolders
    updateSettings({
      trustedFolders: { ...current, denied: current.denied.filter((p) => p !== path) },
    })
  }

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '14px' }}>
      <span
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '14px',
          'font-weight': '500',
          color: '#F5F5F7',
        }}
      >
        Trusted Folders
      </span>

      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '8px' }}>
        {/* Allowed folders */}
        <For each={settings().trustedFolders.allowed}>
          {(path) => (
            <div
              class="flex items-center group"
              style={{
                'border-radius': '8px',
                background: '#111114',
                border: '1px solid #ffffff08',
                padding: '8px 14px',
                gap: '10px',
              }}
            >
              <ShieldCheck
                style={{ width: '14px', height: '14px', color: '#34C759', 'flex-shrink': '0' }}
              />
              <span
                class="flex-1 truncate"
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '13px',
                  color: '#C8C8CC',
                }}
              >
                {path}
              </span>
              <button
                type="button"
                onClick={() => removeAllowed(path)}
                class="opacity-0 group-hover:opacity-100 transition-opacity"
                style={{
                  color: '#48484A',
                  background: 'transparent',
                  border: 'none',
                  padding: '2px',
                  cursor: 'pointer',
                }}
              >
                <X style={{ width: '12px', height: '12px' }} />
              </button>
            </div>
          )}
        </For>

        {/* Denied folders */}
        <For each={settings().trustedFolders.denied}>
          {(path) => (
            <div
              class="flex items-center group"
              style={{
                'border-radius': '8px',
                background: '#111114',
                border: '1px solid #FF453A18',
                padding: '8px 14px',
                gap: '10px',
              }}
            >
              <ShieldX
                style={{ width: '14px', height: '14px', color: '#FF453A', 'flex-shrink': '0' }}
              />
              <span
                class="flex-1 truncate"
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '13px',
                  color: '#C8C8CC',
                }}
              >
                {path}
              </span>
              <button
                type="button"
                onClick={() => removeDenied(path)}
                class="opacity-0 group-hover:opacity-100 transition-opacity"
                style={{
                  color: '#48484A',
                  background: 'transparent',
                  border: 'none',
                  padding: '2px',
                  cursor: 'pointer',
                }}
              >
                <X style={{ width: '12px', height: '12px' }} />
              </button>
            </div>
          )}
        </For>
      </div>

      {/* Add inputs */}
      <div class="flex" style={{ gap: '8px' }}>
        <input
          type="text"
          placeholder="Add allowed path..."
          value={newAllowed()}
          onInput={(e) => setNewAllowed(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') addAllowed()
          }}
          class="flex-1 outline-none"
          style={{
            'border-radius': '8px',
            background: '#ffffff06',
            border: '1px solid #ffffff0a',
            padding: '8px 12px',
            'font-family': 'Geist Mono, monospace',
            'font-size': '12px',
            color: '#F5F5F7',
          }}
        />
        <button
          type="button"
          onClick={addAllowed}
          class="flex items-center"
          style={{
            gap: '4px',
            'border-radius': '8px',
            background: '#34C759',
            color: '#FFFFFF',
            border: 'none',
            padding: '0 12px',
            'font-size': '12px',
            'font-weight': '500',
            cursor: 'pointer',
          }}
        >
          <Plus style={{ width: '12px', height: '12px' }} /> Allow
        </button>
        <button
          type="button"
          onClick={addDenied}
          class="flex items-center"
          style={{
            gap: '4px',
            'border-radius': '8px',
            background: '#FF453A',
            color: '#FFFFFF',
            border: 'none',
            padding: '0 12px',
            'font-size': '12px',
            'font-weight': '500',
            cursor: 'pointer',
          }}
        >
          <Plus style={{ width: '12px', height: '12px' }} /> Deny
        </button>
      </div>
    </div>
  )
}
