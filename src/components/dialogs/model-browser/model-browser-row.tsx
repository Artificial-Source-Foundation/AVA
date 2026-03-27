/**
 * Model Browser Row
 *
 * A single model row inside the grouped list.
 * Shows model name, context window badge, provider name,
 * and a "thinking" badge when the model supports reasoning.
 */

import type { Component } from 'solid-js'
import { Show } from 'solid-js'
import type { BrowsableModel } from './model-browser-types'

interface ModelBrowserRowProps {
  model: BrowsableModel
  isCurrentModel: boolean
  isKeyboardSelected: boolean
  onSelect: () => void
  formatContext: (tokens: number) => string
}

export const ModelBrowserRow: Component<ModelBrowserRowProps> = (props) => {
  const hasThinking = (): boolean =>
    props.model.capabilities.includes('reasoning') || props.model.capabilities.includes('thinking')

  return (
    <button
      type="button"
      data-active={props.isKeyboardSelected ? 'true' : undefined}
      onClick={() => props.onSelect()}
      class="
        w-full flex items-center gap-3
        px-3 py-[7px]
        rounded-[8px]
        text-left
        transition-colors duration-100
        group
      "
      classList={{
        'bg-[var(--accent-subtle)] border-l-2 border-l-[var(--accent)]': props.isCurrentModel,
        'bg-[var(--surface-raised)]': props.isKeyboardSelected && !props.isCurrentModel,
        'hover:bg-[var(--surface-raised)]': !props.isCurrentModel && !props.isKeyboardSelected,
        'border-l-2 border-l-transparent': !props.isCurrentModel,
      }}
    >
      {/* Left: model info */}
      <div class="flex-1 min-w-0">
        <div class="flex items-center gap-2">
          {/* Model name */}
          <span class="text-[13px] font-medium text-[var(--text-primary)] truncate">
            {props.model.name}
          </span>

          {/* Context window badge */}
          <Show when={props.model.contextWindow > 0}>
            <span
              class="
                flex-shrink-0
                px-1.5 py-[1px]
                text-[10px] font-medium
                text-[var(--text-muted)] bg-[var(--gray-7)]/30
                rounded-[4px]
              "
            >
              {props.formatContext(props.model.contextWindow)}
            </span>
          </Show>
        </div>

        {/* Provider name */}
        <span class="text-[11px] text-[var(--text-muted)] truncate block mt-[1px]">
          {props.model.providerName}
        </span>
      </div>

      {/* Right: badges */}
      <div class="flex items-center gap-2 flex-shrink-0">
        {/* Thinking badge */}
        <Show when={hasThinking()}>
          <span
            class="
              px-1.5 py-[1px]
              text-[10px] font-medium
              text-[var(--accent)] bg-[var(--accent-subtle)]
              rounded-[4px]
            "
          >
            thinking
          </span>
        </Show>

        {/* Current model indicator */}
        <Show when={props.isCurrentModel}>
          <span class="w-1.5 h-1.5 rounded-full bg-[var(--accent)] flex-shrink-0" />
        </Show>
      </div>
    </button>
  )
}
