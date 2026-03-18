/**
 * Model Aliases Section
 * Extracted from BehaviorTab — manages short names for model IDs.
 */

import { type Component, For } from 'solid-js'
import { useSettings } from '../../../stores/settings'

export const ModelAliasesSection: Component = () => {
  const { settings, updateSettings } = useSettings()

  return (
    <div>
      <p class="text-[10px] text-[var(--text-muted)] mb-2">
        Create short names for model IDs (e.g. "fast" → "openai/gpt-4o-mini")
      </p>
      <div class="space-y-1.5">
        <For each={Object.entries(settings().modelAliases)}>
          {([alias, modelId]) => (
            <div class="flex items-center gap-2">
              <input
                type="text"
                value={alias}
                onBlur={(e) => {
                  const newAlias = e.currentTarget.value.trim()
                  if (!newAlias || newAlias === alias) return
                  const aliases = { ...settings().modelAliases }
                  delete aliases[alias]
                  aliases[newAlias] = modelId
                  updateSettings({ modelAliases: aliases })
                }}
                class="w-24 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                placeholder="alias"
              />
              <span class="text-[10px] text-[var(--text-muted)]">→</span>
              <input
                type="text"
                value={modelId}
                onBlur={(e) => {
                  const newModelId = e.currentTarget.value.trim()
                  if (!newModelId) return
                  updateSettings({
                    modelAliases: { ...settings().modelAliases, [alias]: newModelId },
                  })
                }}
                class="flex-1 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                placeholder="provider/model-id"
              />
              <button
                type="button"
                onClick={() => {
                  const aliases = { ...settings().modelAliases }
                  delete aliases[alias]
                  updateSettings({ modelAliases: aliases })
                }}
                class="px-1.5 py-1 text-[10px] text-[var(--error)] hover:bg-[var(--alpha-white-5)] rounded-[var(--radius-sm)] transition-colors"
                title="Remove alias"
              >
                x
              </button>
            </div>
          )}
        </For>
      </div>
      <button
        type="button"
        onClick={() => {
          const aliases = { ...settings().modelAliases }
          let name = 'alias'
          let i = 1
          while (aliases[name]) {
            name = `alias-${i++}`
          }
          aliases[name] = ''
          updateSettings({ modelAliases: aliases })
        }}
        class="mt-2 px-3 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)] transition-colors"
      >
        + Add Alias
      </button>
    </div>
  )
}
