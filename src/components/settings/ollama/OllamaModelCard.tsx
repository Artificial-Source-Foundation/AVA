/**
 * Ollama Model Card — single row in the Ollama model list
 */

import { Trash2 } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { formatBytes, type OllamaModel } from './ollama-helpers'

interface OllamaModelCardProps {
  model: OllamaModel
  isDeleting: boolean
  isConfirmingDelete: boolean
  onRequestDelete: () => void
  onConfirmDelete: () => void
  onCancelDelete: () => void
}

export const OllamaModelCard: Component<OllamaModelCardProps> = (props) => {
  return (
    <div class="flex items-center gap-3 px-3 py-2 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-raised)]">
      <div class="flex-1 min-w-0">
        <p class="text-xs font-medium text-[var(--text-primary)] truncate">{props.model.name}</p>
        <div class="flex flex-wrap items-center gap-x-2 gap-y-0.5 mt-0.5">
          <span class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
            {formatBytes(props.model.size)}
          </span>
          <Show when={props.model.family}>
            <span class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
              {props.model.family}
            </span>
          </Show>
          <Show when={props.model.parameterSize}>
            <span class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
              {props.model.parameterSize}
            </span>
          </Show>
          <Show when={props.model.quantizationLevel}>
            <span class="text-[var(--settings-text-badge)] px-1 py-0.5 rounded bg-[var(--surface-sunken)] text-[var(--text-muted)]">
              {props.model.quantizationLevel}
            </span>
          </Show>
        </div>
      </div>
      <Show
        when={!props.isConfirmingDelete}
        fallback={
          <div class="flex items-center gap-1">
            <button
              type="button"
              onClick={props.onConfirmDelete}
              disabled={props.isDeleting}
              class="px-2 py-1 text-[var(--settings-text-badge)] text-white bg-[var(--error)] rounded-[var(--radius-sm)] disabled:opacity-50"
            >
              {props.isDeleting ? 'Deleting...' : 'Confirm'}
            </button>
            <button
              type="button"
              onClick={props.onCancelDelete}
              class="px-2 py-1 text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
            >
              Cancel
            </button>
          </div>
        }
      >
        <button
          type="button"
          onClick={props.onRequestDelete}
          class="p-1.5 text-[var(--text-muted)] hover:text-[var(--error)] rounded-[var(--radius-sm)] hover:bg-[var(--error-subtle)] transition-colors"
          title="Delete model"
        >
          <Trash2 class="w-3.5 h-3.5" />
        </button>
      </Show>
    </div>
  )
}
