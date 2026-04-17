import { X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useSettingsDialogEscape } from './settings-dialog-utils'
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
  let dialogRef: HTMLDivElement | undefined

  useSettingsDialogEscape({
    onEscape: props.onClose,
    isOpen: () => true,
    getDialogElement: () => dialogRef,
  })

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

      let key = e.key.toLowerCase()
      // When Ctrl/Meta remaps a letter to a control-char name (e.g. Ctrl+M → "Enter"),
      // recover the real letter from e.code.
      if (
        (e.ctrlKey || e.metaKey) &&
        e.code &&
        (key === 'enter' || key === 'tab' || key === 'backspace' || key.length > 1) &&
        !['control', 'shift', 'alt', 'meta'].includes(key)
      ) {
        if (e.code.startsWith('Key')) key = e.code.slice(3).toLowerCase()
        else if (e.code.startsWith('Digit')) key = e.code.slice(5)
      }
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
      ref={dialogRef}
      role="dialog"
      data-settings-nested-dialog="true"
      class="fixed inset-0 flex items-center justify-center z-[60] p-4 outline-none"
      style={{ background: 'var(--modal-overlay)' }}
      tabindex="-1"
      onClick={(e) => e.target === e.currentTarget && props.onClose()}
      onKeyDown={(e) => {
        if (e.key !== 'Escape') return
        e.preventDefault()
        e.stopPropagation()
        props.onClose()
      }}
    >
      <div
        style={{
          background: 'var(--modal-surface)',
          border: '1px solid var(--modal-border)',
          'border-radius': 'var(--modal-radius-sm)',
          width: '100%',
          'max-width': '28rem',
          'box-shadow': 'var(--modal-shadow)',
        }}
      >
        <div
          class="flex items-center justify-between"
          style={{
            padding: '12px 20px',
            'border-bottom': '1px solid #ffffff06',
          }}
        >
          <h2
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '14px',
              'font-weight': '600',
              color: '#F5F5F7',
            }}
          >
            Edit Shortcut
          </h2>
          <button
            type="button"
            onClick={() => props.onClose()}
            class="transition-colors"
            style={{
              padding: '6px',
              'border-radius': '6px',
              color: '#48484A',
              background: 'transparent',
              border: 'none',
              cursor: 'pointer',
            }}
          >
            <X class="w-4 h-4" />
          </button>
        </div>

        <div style={{ padding: '20px', display: 'flex', 'flex-direction': 'column', gap: '16px' }}>
          <div>
            <p
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                'font-weight': '500',
                color: '#F5F5F7',
              }}
            >
              {props.keybinding.action}
            </p>
            <p
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#48484A',
                'margin-top': '2px',
              }}
            >
              {props.keybinding.description}
            </p>
          </div>

          <FieldGroup label="Shortcut">
            <button
              type="button"
              onClick={startRecording}
              class="w-full text-center transition-colors"
              style={{
                padding: '12px 16px',
                'border-radius': '8px',
                border: `2px dashed ${recording() ? '#0A84FF' : '#ffffff0a'}`,
                background: recording() ? '#0A84FF10' : '#ffffff06',
                color: recording() ? '#0A84FF' : '#F5F5F7',
                'font-size': '13px',
                cursor: 'pointer',
              }}
            >
              <Show
                when={!recording()}
                fallback={<span style={{ 'font-weight': '500' }}>Press your shortcut...</span>}
              >
                <div class="flex items-center justify-center gap-1">
                  <For each={keys()}>
                    {(key, index) => (
                      <>
                        <kbd
                          style={{
                            padding: '4px 8px',
                            background: '#0A0A0C',
                            border: '1px solid #ffffff08',
                            'border-radius': '6px',
                            'font-family': 'Geist Mono, monospace',
                            'font-size': '12px',
                            'font-weight': '500',
                            color: '#F5F5F7',
                          }}
                        >
                          {formatKey(key)}
                        </kbd>
                        <Show when={index() < keys().length - 1}>
                          <span style={{ color: '#48484A' }}>+</span>
                        </Show>
                      </>
                    )}
                  </For>
                </div>
              </Show>
            </button>
            <p style={{ 'font-size': '11px', color: '#48484A', 'margin-top': '6px' }}>
              Click to record a new shortcut
            </p>
          </FieldGroup>
        </div>

        <div
          class="flex items-center justify-end"
          style={{
            gap: '8px',
            padding: '12px 20px',
            'border-top': '1px solid #ffffff06',
          }}
        >
          <button
            type="button"
            onClick={() => props.onClose()}
            style={{
              padding: '6px 14px',
              'font-size': '13px',
              color: '#48484A',
              background: 'transparent',
              border: 'none',
              'border-radius': '8px',
              cursor: 'pointer',
            }}
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={handleSave}
            disabled={keys().length === 0}
            style={{
              padding: '6px 14px',
              'font-size': '13px',
              'font-weight': '500',
              color: '#FFFFFF',
              background: '#0A84FF',
              border: 'none',
              'border-radius': '8px',
              cursor: 'pointer',
              opacity: keys().length === 0 ? '0.5' : '1',
            }}
          >
            Save
          </button>
        </div>
      </div>
    </div>
  )
}
