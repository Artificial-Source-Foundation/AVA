/**
 * Message Input Component
 *
 * Chat input with model selector, Plan/Act toggle, and Agent mode.
 * Bottom toolbar inspired by OpenCode Desktop.
 */

import {
  ArrowUp,
  Bot,
  ChevronDown,
  FileSearch,
  Shield,
  ShieldAlert,
  ShieldOff,
  Square,
  X,
  Zap,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { useSession } from '../../stores/session'
import type { PermissionMode } from '../../stores/settings'
import { useSettings } from '../../stores/settings'

export const MessageInput: Component = () => {
  const [input, setInput] = createSignal('')
  const [useAgentMode, setUseAgentMode] = createSignal(false)
  const [modelDropdownOpen, setModelDropdownOpen] = createSignal(false)
  let submitting = false
  // oxlint-disable-next-line no-unassigned-vars -- SolidJS ref pattern: assigned via ref={} in JSX
  let textareaRef: HTMLTextAreaElement | undefined

  // Chat mode (simple single-turn)
  const chat = useChat()

  // Agent mode (full autonomous loop)
  const agent = useAgent()

  // Session + Settings for model selection
  const { selectedModel, setSelectedModel } = useSession()
  const { settings, cyclePermissionMode } = useSettings()

  // Permission mode config
  const permissionConfig: Record<
    PermissionMode,
    { icon: typeof Shield; color: string; label: string }
  > = {
    ask: { icon: Shield, color: 'var(--text-muted)', label: 'Ask' },
    'auto-approve': { icon: ShieldAlert, color: 'var(--warning)', label: 'Auto' },
    bypass: { icon: ShieldOff, color: 'var(--error)', label: 'Bypass' },
  }

  // Get enabled providers and their models
  const enabledProviders = createMemo(() =>
    settings().providers.filter((p) => p.enabled && p.models.length > 0)
  )

  // Get the display name for the current model
  const currentModelDisplay = createMemo(() => {
    const modelId = selectedModel()
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model) return model.name
    }
    // Fallback: show raw model ID trimmed
    if (modelId.length > 30) return `${modelId.slice(0, 27)}...`
    return modelId
  })

  const handleSelectModel = (modelId: string) => {
    setSelectedModel(modelId)
    setModelDropdownOpen(false)
  }

  const autoResize = () => {
    if (!textareaRef) return
    textareaRef.style.height = 'auto'
    textareaRef.style.height = `${Math.min(textareaRef.scrollHeight, 200)}px`
  }

  const isProcessing = () => chat.isStreaming() || agent.isRunning()

  const handleSubmit = async (e: Event) => {
    e.preventDefault()
    const message = input().trim()
    if (!message || isProcessing() || submitting) return

    submitting = true
    setInput('')
    if (textareaRef) textareaRef.style.height = 'auto'
    chat.clearError()
    agent.clearError()

    try {
      if (useAgentMode()) {
        await agent.run(message, { model: selectedModel() })
      } else {
        await chat.sendMessage(message, selectedModel())
      }
    } finally {
      submitting = false
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSubmit(e)
    }
  }

  const handleCancel = () => {
    if (useAgentMode()) {
      agent.cancel()
    } else {
      chat.cancel()
    }
  }

  const currentError = () => {
    if (useAgentMode()) {
      return agent.lastError() ? { message: agent.lastError()! } : null
    }
    return chat.error()
  }

  const clearCurrentError = () => {
    if (useAgentMode()) {
      agent.clearError()
    } else {
      chat.clearError()
    }
  }

  return (
    <div class="p-4 border-t border-[var(--border-subtle)]">
      {/* Error display */}
      <Show when={currentError()}>
        <div class="mb-3 p-3 bg-[var(--error-subtle)] border border-[var(--error)] rounded-lg flex items-center justify-between gap-3">
          <span class="text-sm text-[var(--error)]">{currentError()!.message}</span>
          <button
            type="button"
            onClick={clearCurrentError}
            class="p-1 rounded text-[var(--error)] hover:bg-[var(--error-subtle)] transition-colors"
          >
            <X class="w-4 h-4" />
          </button>
        </div>
      </Show>

      {/* Input form */}
      <form onSubmit={handleSubmit} class="space-y-2">
        <div class="relative">
          <textarea
            ref={textareaRef}
            value={input()}
            onInput={(e) => {
              setInput(e.currentTarget.value)
              autoResize()
            }}
            onKeyDown={handleKeyDown}
            placeholder={
              isProcessing()
                ? useAgentMode()
                  ? `Working... (turn ${agent.currentTurn()})`
                  : 'Generating...'
                : agent.isPlanMode()
                  ? 'Plan your approach...'
                  : 'Ask anything...'
            }
            disabled={isProcessing()}
            rows={1}
            class="
              w-full px-4 py-3
              bg-[var(--input-background)] text-[var(--text-primary)]
              placeholder-[var(--input-placeholder)]
              border border-[var(--input-border)] rounded-lg
              text-sm resize-none
              transition-colors
              focus:outline-none focus:border-[var(--input-border-focus)]
              disabled:opacity-50
            "
            style={{ 'min-height': '44px', 'max-height': '200px' }}
          />
        </div>

        {/* Bottom toolbar */}
        <div class="flex items-center justify-between gap-2">
          <div class="flex items-center gap-1.5">
            {/* Model selector */}
            <div class="relative">
              <button
                type="button"
                onClick={() => setModelDropdownOpen(!modelDropdownOpen())}
                class="
                  flex items-center gap-1 px-2 py-1
                  text-[11px] text-[var(--text-secondary)]
                  bg-[var(--surface-raised)]
                  border border-[var(--border-subtle)]
                  rounded-[var(--radius-md)]
                  hover:border-[var(--accent-muted)]
                  transition-colors
                "
              >
                <ChevronDown class="w-3 h-3" />
                <span class="truncate max-w-[140px]">{currentModelDisplay()}</span>
              </button>

              {/* Model dropdown */}
              <Show when={modelDropdownOpen()}>
                {/* biome-ignore lint/a11y/noStaticElementInteractions: backdrop click to close dropdown */}
                {/* biome-ignore lint/a11y/useKeyWithClickEvents: backdrop does not need keyboard interaction */}
                <div class="fixed inset-0 z-40" onClick={() => setModelDropdownOpen(false)} />
                <div
                  class="absolute bottom-full left-0 mb-1 z-50 w-64 max-h-72 overflow-y-auto bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-lg)] shadow-xl"
                  style={{ transform: 'translateZ(0)' }}
                >
                  <For each={enabledProviders()}>
                    {(provider) => (
                      <div>
                        <div class="px-3 py-1.5 text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider sticky top-0 bg-[var(--surface-overlay)]">
                          {provider.name}
                        </div>
                        <For each={provider.models}>
                          {(model) => (
                            <button
                              type="button"
                              onClick={() => handleSelectModel(model.id)}
                              class={`
                                w-full text-left px-3 py-1.5
                                text-xs transition-colors
                                ${
                                  selectedModel() === model.id
                                    ? 'text-[var(--accent)] bg-[var(--accent-subtle)]'
                                    : 'text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-primary)]'
                                }
                              `}
                            >
                              {model.name}
                            </button>
                          )}
                        </For>
                      </div>
                    )}
                  </For>
                  <Show when={enabledProviders().length === 0}>
                    <div class="px-3 py-4 text-center text-xs text-[var(--text-muted)]">
                      No providers configured
                    </div>
                  </Show>
                </div>
              </Show>
            </div>

            {/* Plan/Act toggle */}
            <button
              type="button"
              onClick={() => agent.togglePlanMode()}
              disabled={isProcessing()}
              class={`
                flex items-center gap-1 px-2 py-1
                text-[11px] font-medium rounded-[var(--radius-md)]
                transition-colors
                ${
                  agent.isPlanMode()
                    ? 'bg-[var(--warning-subtle)] text-[var(--warning)] border border-[var(--warning)]'
                    : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)]'
                }
                disabled:opacity-50 disabled:cursor-not-allowed
              `}
            >
              <FileSearch class="w-3 h-3" />
              {agent.isPlanMode() ? 'Plan' : 'Act'}
            </button>

            {/* Agent/Chat toggle */}
            <button
              type="button"
              onClick={() => setUseAgentMode(!useAgentMode())}
              disabled={isProcessing()}
              class={`
                flex items-center gap-1 px-2 py-1
                text-[11px] font-medium rounded-[var(--radius-md)]
                transition-colors
                ${
                  useAgentMode()
                    ? 'bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent)]'
                    : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)]'
                }
                disabled:opacity-50 disabled:cursor-not-allowed
              `}
              title={
                useAgentMode() ? 'Agent mode: Full autonomous loop' : 'Chat mode: Simple responses'
              }
            >
              {useAgentMode() ? <Bot class="w-3 h-3" /> : <Zap class="w-3 h-3" />}
              {useAgentMode() ? 'Agent' : 'Chat'}
            </button>

            {/* Permission mode toggle */}
            {(() => {
              const mode = settings().permissionMode
              const cfg = permissionConfig[mode]
              const Icon = cfg.icon
              return (
                <button
                  type="button"
                  onClick={() => cyclePermissionMode()}
                  class="flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)] transition-colors"
                  title={`Permissions: ${cfg.label} (click to cycle)`}
                >
                  <Icon class="w-3 h-3" style={{ color: cfg.color }} />
                  <span style={{ color: cfg.color }}>{cfg.label}</span>
                </button>
              )
            })()}
          </div>

          {/* Right side: status + send/cancel */}
          <div class="flex items-center gap-2">
            {/* Status indicators */}
            <Show when={agent.isRunning()}>
              <span class="flex items-center gap-1 text-[10px] text-[var(--text-tertiary)]">
                <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
                Turn {agent.currentTurn()}
              </span>
            </Show>
            <Show when={agent.doomLoopDetected()}>
              <span class="text-[10px] text-[var(--warning)]">Loop</span>
            </Show>

            {/* Send / Cancel button */}
            <Show
              when={isProcessing()}
              fallback={
                <button
                  type="submit"
                  disabled={!input().trim()}
                  class="
                    p-2
                    bg-[var(--accent)] hover:bg-[var(--accent-hover)]
                    text-white
                    rounded-[var(--radius-md)]
                    transition-colors
                    disabled:opacity-30 disabled:cursor-not-allowed
                  "
                >
                  <ArrowUp class="w-4 h-4" />
                </button>
              }
            >
              <button
                type="button"
                onClick={handleCancel}
                class="
                  p-2
                  bg-[var(--error)] hover:brightness-110
                  text-white
                  rounded-[var(--radius-md)]
                  transition-colors
                "
              >
                <Square class="w-4 h-4" />
              </button>
            </Show>
          </div>
        </div>
      </form>
    </div>
  )
}
