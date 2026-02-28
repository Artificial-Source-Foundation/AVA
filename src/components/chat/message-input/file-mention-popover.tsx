/**
 * File Mention Popover
 *
 * Autocomplete dropdown triggered by @ in the message input.
 * Receives pre-filtered files from the parent component.
 */

import { File, Folder } from 'lucide-solid'
import { type Accessor, type Component, For, Show } from 'solid-js'
import type { SearchableFile } from '../../../services/file-search'

interface FileMentionPopoverProps {
  /** Whether the popover is visible */
  open: Accessor<boolean>
  /** Pre-filtered file results */
  files: Accessor<SearchableFile[]>
  /** Called when user selects a file */
  onSelect: (file: SearchableFile) => void
  /** Current keyboard-selected index */
  selectedIndex: Accessor<number>
}

export const FileMentionPopover: Component<FileMentionPopoverProps> = (props) => {
  return (
    <Show when={props.open() && props.files().length > 0}>
      <div
        class="
          absolute bottom-full left-0 right-0 mb-1 z-[var(--z-popover)]
          max-h-[280px] overflow-y-auto
          bg-[var(--surface-overlay)] border border-[var(--border-default)]
          rounded-[var(--radius-lg)] shadow-[var(--shadow-lg)]
          animate-dropdown-in
        "
        role="listbox"
      >
        <div class="py-1">
          <For each={props.files()}>
            {(file, index) => (
              <button
                type="button"
                role="option"
                aria-selected={index() === props.selectedIndex()}
                class="flex items-center gap-2 w-full px-3 py-1.5 text-left text-xs transition-colors"
                classList={{
                  'bg-[var(--accent-subtle)] text-[var(--text-primary)]':
                    index() === props.selectedIndex(),
                  'text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)]':
                    index() !== props.selectedIndex(),
                }}
                onClick={() => props.onSelect(file)}
              >
                {file.isDir ? (
                  <Folder class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />
                ) : (
                  <File class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />
                )}
                <span class="truncate font-[var(--font-mono)]">{file.relative}</span>
              </button>
            )}
          </For>
        </div>
        <div class="px-3 py-1 border-t border-[var(--border-subtle)] text-[9px] text-[var(--text-muted)]">
          <kbd class="font-[var(--font-mono)]">↑↓</kbd> navigate &middot;{' '}
          <kbd class="font-[var(--font-mono)]">Tab</kbd> select &middot;{' '}
          <kbd class="font-[var(--font-mono)]">Esc</kbd> dismiss
        </div>
      </div>
    </Show>
  )
}
