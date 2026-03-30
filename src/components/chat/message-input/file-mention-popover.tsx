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
    <Show when={props.open()}>
      <div
        class="
          absolute bottom-full left-0 right-0 mb-1 z-[var(--z-popover)]
          max-h-[280px] overflow-y-auto
          animate-dropdown-in
        "
        style={{
          background: 'var(--dropdown-surface)',
          border: '1px solid var(--dropdown-border)',
          'border-radius': 'var(--dropdown-radius)',
          'box-shadow': 'var(--modal-shadow)',
        }}
        role="listbox"
      >
        <Show
          when={props.files().length > 0}
          fallback={
            <div
              class="px-3 py-4 text-center"
              style={{ 'font-size': '12px', color: 'var(--gray-6)' }}
            >
              Loading project files...
            </div>
          }
        >
          <div class="py-1">
            <For each={props.files()}>
              {(file, index) => {
                const isSelected = () => index() === props.selectedIndex()
                return (
                  <button
                    type="button"
                    role="option"
                    aria-selected={isSelected()}
                    class="flex items-center gap-2 w-full px-3 text-left transition-colors"
                    style={{
                      height: '34px',
                      'font-size': '13px',
                      'font-family': 'var(--font-sans)',
                      background: isSelected() ? 'var(--dropdown-item-active-bg)' : 'transparent',
                      color: isSelected() ? 'var(--dropdown-item-active-text)' : 'var(--gray-9)',
                    }}
                    onClick={() => props.onSelect(file)}
                  >
                    {file.isDir ? (
                      <Folder
                        class="w-3.5 h-3.5 flex-shrink-0"
                        style={{
                          color: isSelected()
                            ? 'var(--dropdown-item-active-text)'
                            : 'var(--gray-6)',
                        }}
                      />
                    ) : (
                      <File
                        class="w-3.5 h-3.5 flex-shrink-0"
                        style={{
                          color: isSelected()
                            ? 'var(--dropdown-item-active-text)'
                            : 'var(--gray-6)',
                        }}
                      />
                    )}
                    <span class="truncate font-[var(--font-mono)]">{file.relative}</span>
                  </button>
                )
              }}
            </For>
          </div>
        </Show>
        <div
          class="px-3 py-1 text-[9px]"
          style={{ 'border-top': '1px solid var(--dropdown-separator)', color: 'var(--gray-6)' }}
        >
          <kbd class="font-[var(--font-mono)]">↑↓</kbd> navigate &middot;{' '}
          <kbd class="font-[var(--font-mono)]">Tab</kbd> select &middot;{' '}
          <kbd class="font-[var(--font-mono)]">Esc</kbd> dismiss
        </div>
      </div>
    </Show>
  )
}
