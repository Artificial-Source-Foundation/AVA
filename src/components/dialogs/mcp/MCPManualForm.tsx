/**
 * MCP Manual Configuration Form
 *
 * Form fields for manually configuring an MCP server:
 * name, transport, command/args/url, cwd, env, trust level.
 */

import { type Component, For, Show } from 'solid-js'
import type { MCPTransportType } from '../../../stores/settings/settings-types'

export interface MCPManualFormProps {
  name: () => string
  setName: (v: string) => void
  transport: () => MCPTransportType
  setTransport: (v: MCPTransportType) => void
  command: () => string
  setCommand: (v: string) => void
  args: () => string
  setArgs: (v: string) => void
  url: () => string
  setUrl: (v: string) => void
  cwd: () => string
  setCwd: (v: string) => void
  envPairs: () => string
  setEnvPairs: (v: string) => void
  trust: () => 'full' | 'sandbox' | 'none'
  setTrust: (v: 'full' | 'sandbox' | 'none') => void
}

const inputClass =
  'w-full mt-1 px-2.5 py-1.5 text-xs rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none'

const monoInputClass = `${inputClass} font-mono`

export const MCPManualForm: Component<MCPManualFormProps> = (props) => {
  return (
    <div class="space-y-3">
      {/* Name */}
      <label class="block">
        <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">Name *</span>
        <input
          type="text"
          value={props.name()}
          onInput={(e) => props.setName(e.currentTarget.value)}
          placeholder="my-server"
          class={inputClass}
        />
      </label>

      {/* Transport */}
      <div>
        <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">Transport</span>
        <div class="flex gap-1 mt-1">
          <For each={['stdio', 'sse', 'http'] as MCPTransportType[]}>
            {(t) => (
              <button
                type="button"
                onClick={() => props.setTransport(t)}
                class={`px-2.5 py-1 text-[11px] rounded-[var(--radius-md)] transition-colors ${
                  props.transport() === t
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
      <Show when={props.transport() === 'stdio'}>
        <label class="block">
          <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
            Command *
          </span>
          <input
            type="text"
            value={props.command()}
            onInput={(e) => props.setCommand(e.currentTarget.value)}
            placeholder="npx"
            class={monoInputClass}
          />
        </label>
        <label class="block">
          <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
            Arguments
          </span>
          <input
            type="text"
            value={props.args()}
            onInput={(e) => props.setArgs(e.currentTarget.value)}
            placeholder="-y @modelcontextprotocol/server-name"
            class={monoInputClass}
          />
        </label>
      </Show>

      {/* URL (sse/http) */}
      <Show when={props.transport() !== 'stdio'}>
        <label class="block">
          <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">URL *</span>
          <input
            type="text"
            value={props.url()}
            onInput={(e) => props.setUrl(e.currentTarget.value)}
            placeholder="http://localhost:3001"
            class={monoInputClass}
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
          value={props.cwd()}
          onInput={(e) => props.setCwd(e.currentTarget.value)}
          placeholder="/path/to/project"
          class={monoInputClass}
        />
      </label>

      {/* Environment Variables */}
      <label class="block">
        <span class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">
          Environment Variables (KEY=value, one per line)
        </span>
        <textarea
          value={props.envPairs()}
          onInput={(e) => props.setEnvPairs(e.currentTarget.value)}
          placeholder="BRAVE_API_KEY=your_key"
          rows={2}
          class={`${monoInputClass} resize-none`}
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
                onClick={() => props.setTrust(level)}
                class={`px-2.5 py-1 text-[11px] rounded-[var(--radius-md)] transition-colors ${
                  props.trust() === level
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
  )
}
