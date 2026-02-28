/**
 * Add MCP Server Dialog
 *
 * Two tabs: "Browse Presets" for popular servers, "Manual" for custom config.
 */

import { Plus, Server } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { MCP_CATEGORIES, MCP_PRESETS, type MCPPreset } from '../../config/mcp-presets'
import type { MCPServerConfig, MCPTransportType } from '../../stores/settings/settings-types'

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

  const resetForm = () => {
    setName('')
    setTransport('stdio')
    setCommand('')
    setArgs('')
    setUrl('')
    setCwd('')
    setEnvPairs('')
    setTrust('full')
  }

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
    resetForm()
    props.onClose()
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
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] w-full max-w-lg shadow-2xl overflow-hidden">
          {/* Header */}
          <div class="flex items-center gap-2 px-4 py-3 border-b border-[var(--border-subtle)]">
            <Server class="w-4 h-4 text-[var(--accent)]" />
            <h3 class="text-sm font-semibold text-[var(--text-primary)] flex-1">Add MCP Server</h3>
            <button
              type="button"
              onClick={() => {
                resetForm()
                props.onClose()
              }}
              class="text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
            >
              Cancel
            </button>
          </div>

          {/* Tabs */}
          <div class="flex border-b border-[var(--border-subtle)]">
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
              <div class="space-y-3">
                {/* Name */}
                <label class="block">
                  <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                    Name *
                  </span>
                  <input
                    type="text"
                    value={name()}
                    onInput={(e) => setName(e.currentTarget.value)}
                    placeholder="my-server"
                    class="w-full mt-1 px-2.5 py-1.5 text-xs rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                  />
                </label>

                {/* Transport */}
                <div>
                  <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                    Transport
                  </span>
                  <div class="flex gap-1 mt-1">
                    <For each={['stdio', 'sse', 'http'] as MCPTransportType[]}>
                      {(t) => (
                        <button
                          type="button"
                          onClick={() => setTransport(t)}
                          class={`px-2.5 py-1 text-[11px] rounded-[var(--radius-md)] transition-colors ${
                            transport() === t
                              ? 'bg-[var(--accent)] text-white'
                              : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
                          }`}
                        >
                          {t.toUpperCase()}
                        </button>
                      )}
                    </For>
                  </div>
                </div>

                {/* Command + Args (stdio) */}
                <Show when={transport() === 'stdio'}>
                  <label class="block">
                    <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                      Command *
                    </span>
                    <input
                      type="text"
                      value={command()}
                      onInput={(e) => setCommand(e.currentTarget.value)}
                      placeholder="npx"
                      class="w-full mt-1 px-2.5 py-1.5 text-xs font-mono rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                    />
                  </label>
                  <label class="block">
                    <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                      Arguments
                    </span>
                    <input
                      type="text"
                      value={args()}
                      onInput={(e) => setArgs(e.currentTarget.value)}
                      placeholder="-y @modelcontextprotocol/server-name"
                      class="w-full mt-1 px-2.5 py-1.5 text-xs font-mono rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                    />
                  </label>
                </Show>

                {/* URL (sse/http) */}
                <Show when={transport() !== 'stdio'}>
                  <label class="block">
                    <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                      URL *
                    </span>
                    <input
                      type="text"
                      value={url()}
                      onInput={(e) => setUrl(e.currentTarget.value)}
                      placeholder="http://localhost:3001"
                      class="w-full mt-1 px-2.5 py-1.5 text-xs font-mono rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                    />
                  </label>
                </Show>

                {/* CWD */}
                <label class="block">
                  <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                    Working Directory (optional)
                  </span>
                  <input
                    type="text"
                    value={cwd()}
                    onInput={(e) => setCwd(e.currentTarget.value)}
                    placeholder="/path/to/project"
                    class="w-full mt-1 px-2.5 py-1.5 text-xs font-mono rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                  />
                </label>

                {/* Environment Variables */}
                <label class="block">
                  <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                    Environment Variables (KEY=value, one per line)
                  </span>
                  <textarea
                    value={envPairs()}
                    onInput={(e) => setEnvPairs(e.currentTarget.value)}
                    placeholder="BRAVE_API_KEY=your_key"
                    rows={2}
                    class="w-full mt-1 px-2.5 py-1.5 text-xs font-mono rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none resize-none"
                  />
                </label>

                {/* Trust Level */}
                <div>
                  <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
                    Trust Level
                  </span>
                  <div class="flex gap-1 mt-1">
                    <For each={['full', 'sandbox', 'none'] as const}>
                      {(level) => (
                        <button
                          type="button"
                          onClick={() => setTrust(level)}
                          class={`px-2.5 py-1 text-[11px] rounded-[var(--radius-md)] transition-colors ${
                            trust() === level
                              ? 'bg-[var(--accent)] text-white'
                              : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
                          }`}
                        >
                          {level.charAt(0).toUpperCase() + level.slice(1)}
                        </button>
                      )}
                    </For>
                  </div>
                </div>
              </div>
            </Show>
          </div>

          {/* Footer */}
          <Show when={tab() === 'manual'}>
            <div class="flex justify-end gap-2 px-4 py-3 border-t border-[var(--border-subtle)]">
              <button
                type="button"
                onClick={() => {
                  resetForm()
                  props.onClose()
                }}
                class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={handleSave}
                disabled={!canSave()}
                class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-50 flex items-center gap-1"
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
