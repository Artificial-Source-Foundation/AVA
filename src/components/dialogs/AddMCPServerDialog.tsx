/**
 * Add MCP Server Dialog
 *
 * Two tabs: "Browse Presets" for popular servers, "Manual" for custom config.
 */

import { Plus, Server } from 'lucide-solid'
import { type Component, createEffect, createSignal, For, on, Show } from 'solid-js'
import { MCP_CATEGORIES, MCP_PRESETS, type MCPPreset } from '../../config/mcp-presets'
import type { MCPServerConfig, MCPTransportType } from '../../stores/settings/settings-types'
import { useSettingsDialogEscape } from '../settings/settings-dialog-utils'
import { MCPManualForm } from './mcp/MCPManualForm'

interface AddMCPServerDialogProps {
  open: boolean
  onClose: () => void
  onSave: (config: MCPServerConfig) => void
}

type Tab = 'presets' | 'manual'

export const AddMCPServerDialog: Component<AddMCPServerDialogProps> = (props) => {
  const [tab, setTab] = createSignal<Tab>('presets')
  const [name, setName] = createSignal('')
  const [transport, setTransport] = createSignal<MCPTransportType>('stdio')
  const [command, setCommand] = createSignal('')
  const [args, setArgs] = createSignal('')
  const [url, setUrl] = createSignal('')
  const [cwd, setCwd] = createSignal('')
  const [envPairs, setEnvPairs] = createSignal('')
  const [trust, setTrust] = createSignal<'full' | 'sandbox' | 'none'>('full')
  let dialogRef: HTMLDivElement | undefined

  const handleClose = () => {
    resetForm()
    props.onClose()
  }

  useSettingsDialogEscape({
    onEscape: handleClose,
    isOpen: () => props.open,
    getDialogElement: () => dialogRef,
  })

  const resetForm = () => {
    setTab('presets')
    setName('')
    setTransport('stdio')
    setCommand('')
    setArgs('')
    setUrl('')
    setCwd('')
    setEnvPairs('')
    setTrust('full')
  }

  createEffect(
    on(
      () => props.open,
      (open, wasOpen) => {
        if (open && wasOpen === false) {
          resetForm()
        }
      },
      { defer: true }
    )
  )

  const applyPreset = (preset: MCPPreset) => {
    setTab('manual')
    setName(preset.name)
    setTransport(preset.type)
    setCommand(preset.command ?? '')
    setArgs(preset.args?.join(' ') ?? '')
    setUrl(preset.url ?? '')
    setTrust('full')
  }

  const handleSave = () => {
    const n = name().trim()
    if (!n) return

    const envObj: Record<string, string> = {}
    for (const line of envPairs().split('\n')) {
      const eq = line.indexOf('=')
      if (eq > 0) envObj[line.slice(0, eq).trim()] = line.slice(eq + 1).trim()
    }

    const config: MCPServerConfig = {
      name: n,
      type: transport(),
      ...(transport() === 'stdio'
        ? { command: command(), args: args().split(/\s+/).filter(Boolean) }
        : { url: url() }),
      ...(cwd() ? { cwd: cwd() } : {}),
      ...(Object.keys(envObj).length > 0 ? { env: envObj } : {}),
      trust: trust(),
    }

    props.onSave(config)
    handleClose()
  }

  const canSave = () => {
    if (!name().trim()) return false
    if (transport() === 'stdio' && !command().trim()) return false
    if (transport() !== 'stdio' && !url().trim()) return false
    return true
  }

  const presetsByCategory = () => {
    const groups: Record<string, MCPPreset[]> = {}
    for (const cat of MCP_CATEGORIES) {
      groups[cat] = MCP_PRESETS.filter((p) => p.category === cat)
    }
    return groups
  }

  return (
    <Show when={props.open}>
      {/* biome-ignore lint/a11y/noStaticElementInteractions: modal wrapper needs Escape handling */}
      <div
        ref={dialogRef}
        data-settings-nested-dialog="true"
        class="fixed inset-0 z-50 flex items-center justify-center outline-none"
        style={{ background: 'var(--modal-overlay)' }}
        tabindex="-1"
        role="dialog"
        aria-modal="true"
        aria-label="Add MCP Server"
      >
        <div
          class="w-full max-w-lg overflow-hidden"
          style={{
            background: 'var(--modal-surface)',
            border: '1px solid var(--modal-border)',
            'border-radius': 'var(--modal-radius-lg)',
            'box-shadow': 'var(--modal-shadow)',
          }}
        >
          {/* Header */}
          <div
            class="flex items-center gap-2 px-4 py-3"
            style={{ 'border-bottom': '1px solid var(--modal-border)' }}
          >
            <Server class="w-4 h-4 text-[var(--accent)]" />
            <h3 class="text-sm font-semibold text-[var(--text-primary)] flex-1">Add MCP Server</h3>
            <button
              type="button"
              onClick={handleClose}
              class="text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
            >
              Cancel
            </button>
          </div>

          {/* Tabs */}
          <div class="flex" style={{ 'border-bottom': '1px solid var(--modal-border)' }}>
            <button
              type="button"
              onClick={() => setTab('presets')}
              class={`flex-1 px-4 py-2 text-xs font-medium transition-colors ${
                tab() === 'presets'
                  ? 'text-[var(--accent)] border-b-2 border-[var(--accent)]'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)]'
              }`}
            >
              Browse Presets
            </button>
            <button
              type="button"
              onClick={() => setTab('manual')}
              class={`flex-1 px-4 py-2 text-xs font-medium transition-colors ${
                tab() === 'manual'
                  ? 'text-[var(--accent)] border-b-2 border-[var(--accent)]'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)]'
              }`}
            >
              Manual Configuration
            </button>
          </div>

          {/* Content */}
          <div class="max-h-80 overflow-y-auto p-4">
            <Show when={tab() === 'presets'}>
              <For each={Object.entries(presetsByCategory())}>
                {([category, presets]) => (
                  <div class="mb-3">
                    <h4 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-1.5">
                      {category}
                    </h4>
                    <div class="grid grid-cols-2 gap-2">
                      <For each={presets}>
                        {(preset) => (
                          <button
                            type="button"
                            onClick={() => applyPreset(preset)}
                            class="text-left p-2.5 rounded-[var(--radius-md)] border border-[var(--border-subtle)] hover:border-[var(--accent)] hover:bg-[var(--accent-subtle)] transition-colors group"
                          >
                            <div class="text-xs font-medium text-[var(--text-primary)] group-hover:text-[var(--accent)]">
                              {preset.name}
                            </div>
                            <div class="text-[10px] text-[var(--text-muted)] line-clamp-2">
                              {preset.description}
                            </div>
                          </button>
                        )}
                      </For>
                    </div>
                  </div>
                )}
              </For>
            </Show>

            <Show when={tab() === 'manual'}>
              <MCPManualForm
                name={name}
                setName={setName}
                transport={transport}
                setTransport={setTransport}
                command={command}
                setCommand={setCommand}
                args={args}
                setArgs={setArgs}
                url={url}
                setUrl={setUrl}
                cwd={cwd}
                setCwd={setCwd}
                envPairs={envPairs}
                setEnvPairs={setEnvPairs}
                trust={trust}
                setTrust={setTrust}
              />
            </Show>
          </div>

          {/* Footer */}
          <Show when={tab() === 'manual'}>
            <div
              class="flex justify-end gap-2 px-4 py-3"
              style={{ 'border-top': '1px solid var(--modal-border)' }}
            >
              <button
                type="button"
                onClick={handleClose}
                class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={handleSave}
                disabled={!canSave()}
                class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:bg-[var(--accent-hover)] transition-colors disabled:opacity-50 flex items-center gap-1"
              >
                <Plus class="w-3 h-3" />
                Add Server
              </button>
            </div>
          </Show>
        </div>
      </div>
    </Show>
  )
}
