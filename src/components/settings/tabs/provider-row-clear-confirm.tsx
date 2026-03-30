import { AlertTriangle } from 'lucide-solid'
import type { Component } from 'solid-js'

interface ProviderRowClearConfirmProps {
  providerName: string
  hasOAuth: boolean
  hasApiKey: boolean
  onConfirm: () => void
  onCancel: () => void
}

export const ProviderRowClearConfirm: Component<ProviderRowClearConfirmProps> = (props) => {
  const clearTarget = () => {
    if (props.hasOAuth && props.hasApiKey) return 'the API key and OAuth connection'
    if (props.hasOAuth) return 'the OAuth connection'
    return 'the API key'
  }

  return (
    <div class="flex flex-col gap-2 p-2.5 bg-[rgba(239,68,68,0.08)] border border-[rgba(239,68,68,0.25)] rounded-[var(--radius-md)]">
      <div class="flex items-start gap-2">
        <AlertTriangle class="w-3.5 h-3.5 text-[var(--error)] flex-shrink-0 mt-0.5" />
        <div class="flex-1 min-w-0">
          <p class="text-[var(--settings-text-button)] font-medium text-[var(--error)]">
            Clear all credentials?
          </p>
          <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)] mt-0.5">
            This will remove {clearTarget()} for {props.providerName}. You will need to
            re-authenticate.
          </p>
        </div>
      </div>
      <div class="flex items-center gap-2 ml-5.5">
        <button
          type="button"
          onClick={() => props.onConfirm()}
          class="px-2.5 py-1 text-[var(--settings-text-badge)] font-medium text-white bg-[var(--error)] rounded-[var(--radius-md)] hover:bg-[color-mix(in_srgb,var(--error)_88%,white_12%)] transition-colors"
        >
          Yes, clear
        </button>
        <button
          type="button"
          onClick={() => props.onCancel()}
          class="px-2.5 py-1 text-[var(--settings-text-badge)] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--border-default)] transition-colors"
        >
          Cancel
        </button>
      </div>
    </div>
  )
}
