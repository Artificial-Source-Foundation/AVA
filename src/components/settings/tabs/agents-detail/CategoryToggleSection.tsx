/**
 * Category Toggle Section
 *
 * Renders a categorized list of chip toggles (tools or capabilities).
 * Each category has a label and a row of toggleable chips.
 */

import type { Component } from 'solid-js'
import { For, Show } from 'solid-js'
import { Chip } from './Chip'

interface CategoryToggleSectionProps {
  /** Section heading, e.g. "Tools" or "Capabilities" */
  title: string
  /** e.g. "3/41" or "5" */
  countLabel: string
  /** Categories with labels and item lists */
  categories: ReadonlyArray<{ label: string; items: readonly string[] }>
  /** Currently selected items */
  selected: string[]
  /** Called when an item is toggled */
  onToggle: (item: string) => void
  /** Optional "select all / none" handlers */
  onSelectAll?: () => void
  onSelectNone?: () => void
}

export const CategoryToggleSection: Component<CategoryToggleSectionProps> = (props) => {
  return (
    <div>
      <div class="flex items-center justify-between mb-2">
        <span class="text-[var(--settings-text-label)] font-medium text-[var(--text-secondary)]">
          {props.title}{' '}
          <span class="text-[var(--text-muted)] font-normal">({props.countLabel})</span>
        </span>
        <Show when={props.onSelectAll && props.onSelectNone}>
          <div class="flex gap-2">
            <button
              type="button"
              onClick={() => props.onSelectAll?.()}
              class="text-[var(--settings-text-button)] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            >
              All
            </button>
            <button
              type="button"
              onClick={() => props.onSelectNone?.()}
              class="text-[var(--settings-text-button)] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            >
              None
            </button>
          </div>
        </Show>
      </div>
      <div class="space-y-1.5">
        <For each={props.categories}>
          {(cat) => (
            <div class="flex items-start gap-2">
              <span class="text-[var(--settings-text-button)] text-[var(--text-muted)] w-16 flex-shrink-0 pt-0.5 text-right">
                {cat.label}
              </span>
              <div class="flex flex-wrap gap-1">
                <For each={cat.items}>
                  {(item) => (
                    <Chip
                      label={item}
                      active={props.selected.includes(item)}
                      onClick={() => props.onToggle(item)}
                    />
                  )}
                </For>
              </div>
            </div>
          )}
        </For>
      </div>
    </div>
  )
}
