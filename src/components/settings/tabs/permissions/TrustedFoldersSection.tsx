/**
 * Trusted Folders Section
 *
 * Unified trusted-folder editor used inside Permissions & Trust.
 */

import { Plus, ShieldCheck, ShieldX, X } from 'lucide-solid'
import { type Component, createSignal, For } from 'solid-js'
import type { TrustedFoldersSettings } from '../../../../stores/settings/settings-types'

export interface TrustedFoldersSectionProps {
  trustedFolders: TrustedFoldersSettings
  onUpdate: (folders: TrustedFoldersSettings) => void
}

export const TrustedFoldersSection: Component<TrustedFoldersSectionProps> = (props) => {
  const [newPath, setNewPath] = createSignal('')

  const normalizePath = (value: string) => value.trim().replace(/\/+$/, '')

  const addAllowed = () => {
    const path = normalizePath(newPath())
    if (!path) return
    const current = props.trustedFolders
    if (current.allowed.includes(path)) return
    props.onUpdate({ ...current, allowed: [...current.allowed, path] })
    setNewPath('')
  }

  const addDenied = () => {
    const path = normalizePath(newPath())
    if (!path) return
    const current = props.trustedFolders
    if (current.denied.includes(path)) return
    props.onUpdate({ ...current, denied: [...current.denied, path] })
    setNewPath('')
  }

  const removeAllowed = (path: string) => {
    const current = props.trustedFolders
    props.onUpdate({ ...current, allowed: current.allowed.filter((p) => p !== path) })
  }

  const removeDenied = (path: string) => {
    const current = props.trustedFolders
    props.onUpdate({ ...current, denied: current.denied.filter((p) => p !== path) })
  }

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '14px' }}>
      <div style={{ display: 'flex', 'flex-direction': 'column', gap: '8px' }}>
        {/* Allowed folders */}
        <For each={props.trustedFolders.allowed}>
          {(path) => (
            <div
              class="flex items-center group"
              style={{
                'border-radius': '8px',
                background: 'var(--surface-raised)',
                border: '1px solid var(--border-subtle)',
                padding: '8px 14px',
                gap: '10px',
              }}
            >
              <ShieldCheck
                style={{
                  width: '14px',
                  height: '14px',
                  color: 'var(--success)',
                  'flex-shrink': '0',
                }}
              />
              <span
                class="flex-1 truncate"
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '13px',
                  color: 'var(--text-secondary)',
                }}
              >
                {path}
              </span>
              <button
                type="button"
                onClick={() => removeAllowed(path)}
                class="opacity-0 group-hover:opacity-100 transition-opacity"
                style={{
                  color: 'var(--text-muted)',
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
        <For each={props.trustedFolders.denied}>
          {(path) => (
            <div
              class="flex items-center group"
              style={{
                'border-radius': '8px',
                background: 'var(--surface-raised)',
                border: '1px solid color-mix(in srgb, var(--error) 25%, transparent)',
                padding: '8px 14px',
                gap: '10px',
              }}
            >
              <ShieldX
                style={{ width: '14px', height: '14px', color: 'var(--error)', 'flex-shrink': '0' }}
              />
              <span
                class="flex-1 truncate"
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '13px',
                  color: 'var(--text-secondary)',
                }}
              >
                {path}
              </span>
              <button
                type="button"
                onClick={() => removeDenied(path)}
                class="opacity-0 group-hover:opacity-100 transition-opacity"
                style={{
                  color: 'var(--text-muted)',
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
          placeholder="Add trusted or denied path..."
          value={newPath()}
          onInput={(e) => setNewPath(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') addAllowed()
          }}
          class="flex-1 outline-none"
          style={{
            'border-radius': '8px',
            background: 'var(--surface-sunken)',
            border: '1px solid var(--border-default)',
            padding: '8px 12px',
            'font-family': 'Geist Mono, monospace',
            'font-size': '12px',
            color: 'var(--text-primary)',
          }}
        />
        <button
          type="button"
          onClick={addAllowed}
          class="flex items-center"
          style={{
            gap: '4px',
            'border-radius': '8px',
            background: 'var(--success)',
            color: 'var(--text-on-accent)',
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
            background: 'var(--error)',
            color: 'var(--text-on-accent)',
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
