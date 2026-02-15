import { X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { FieldGroup } from './settings-field-group'
import type { Keybinding } from './tabs/KeybindingsTab'

interface KeybindingEditModalProps {
  keybinding: Keybinding
  onClose: () => void
  onSave: (kb: Keybinding) => void
}

export const KeybindingEditModal: Component<KeybindingEditModalProps> = (props) => {
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [keys, setKeys] = createSignal<string[]>([...props.keybinding.keys])
  const [recording, setRecording] = createSignal(false)

  const startRecording = () => {
    setRecording(true)
    setKeys([])

    const handler = (e: KeyboardEvent) => {
      e.preventDefault()
      e.stopPropagation()

      const newKeys: string[] = []
      if (e.ctrlKey || e.metaKey) newKeys.push('meta')
      if (e.shiftKey) newKeys.push('shift')
      if (e.altKey) newKeys.push('alt')

      const key = e.key.toLowerCase()
      if (!['control', 'shift', 'alt', 'meta'].includes(key)) {
        newKeys.push(key)
      }

      if (newKeys.some((k) => !['meta', 'shift', 'alt'].includes(k))) {
        setKeys(newKeys)
        setRecording(false)
        document.removeEventListener('keydown', handler, true)
      }
    }

    document.addEventListener('keydown', handler, true)
  }

  const handleSave = () => {
    if (keys().length > 0) {
      props.onSave({ ...props.keybinding, keys: keys() })
    }
  }

  const formatKey = (key: string): string => {
    const keyMap: Record<string, string> = {
      meta: 'Ctrl',
      shift: 'Shift',
      alt: 'Alt',
      enter: 'Enter',
      escape: 'Esc',
      backspace: 'Bksp',
      arrowup: 'Up',
      arrowdown: 'Down',
      arrowleft: 'Left',
      arrowright: 'Right',
    }
    return keyMap[key] || key.toUpperCase()
  }

  return (
    <div
      role="dialog"
      class="fixed inset-0 bg-black/60 flex items-center justify-center z-[60] p-4"
      onClick={(e) => e.target === e.currentTarget && props.onClose()}
      onKeyDown={(e) => e.key === 'Escape' && props.onClose()}
    >
      <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] w-full max-w-md shadow-xl">
        <div class="flex items-center justify-between px-5 py-3 border-b border-[var(--border-subtle)]">
          <h2 class="text-sm font-semibold text-[var(--text-primary)]">Edit Shortcut</h2>
          <button
            type="button"
            onClick={() => props.onClose()}
            class="p-1.5 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
          >
            <X class="w-4 h-4" />
          </button>
        </div>

        <div class="p-5 space-y-4">
          <div>
            <p class="text-sm font-medium text-[var(--text-primary)]">{props.keybinding.action}</p>
            <p class="text-xs text-[var(--text-muted)] mt-0.5">{props.keybinding.description}</p>
          </div>

          <FieldGroup label="Shortcut">
            <button
              type="button"
              onClick={startRecording}
              class={`w-full px-4 py-3 text-center text-sm rounded-[var(--radius-lg)] border-2 border-dashed transition-colors duration-[var(--duration-fast)] ${recording() ? 'border-[var(--accent)] bg-[var(--accent-subtle)] text-[var(--accent)] animate-pulse' : 'border-[var(--border-default)] bg-[var(--input-background)] text-[var(--text-primary)] hover:border-[var(--accent-muted)]'}`}
            >
              <Show
                when={!recording()}
                fallback={<span class="font-medium">Press your shortcut...</span>}
              >
                <div class="flex items-center justify-center gap-1">
                  <For each={keys()}>
                    {(key, index) => (
                      <>
                        <kbd class="px-2 py-1 bg-[var(--surface-raised)] border border-[var(--border-default)] rounded-md text-xs font-mono font-medium shadow-[0_1px_0_var(--alpha-black-20)]">
                          {formatKey(key)}
                        </kbd>
                        <Show when={index() < keys().length - 1}>
                          <span class="text-[var(--text-muted)]">+</span>
                        </Show>
                      </>
                    )}
                  </For>
                </div>
              </Show>
            </button>
            <p class="text-[10px] text-[var(--text-muted)] mt-1.5">
              Click to record a new shortcut
            </p>
          </FieldGroup>
        </div>

        <div class="flex items-center justify-end gap-2 px-5 py-3 border-t border-[var(--border-subtle)]">
          <button
            type="button"
            onClick={() => props.onClose()}
            class="px-3 py-1.5 text-xs text-[var(--text-secondary)] hover:text-[var(--text-primary)] rounded-[var(--radius-md)] transition-colors"
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={handleSave}
            disabled={keys().length === 0}
            class="px-3 py-1.5 text-xs bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white rounded-[var(--radius-md)] font-medium transition-colors disabled:opacity-50"
          >
            Save
          </button>
        </div>
      </div>
    </div>
  )
}
