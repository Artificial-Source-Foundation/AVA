/**
 * Export Options Dialog
 *
 * 440px card-style modal with format radio group (Markdown/JSON/Plain Text),
 * include checkboxes (tool outputs, thinking blocks, system messages),
 * and footer with Cancel (ghost) + Export (blue with download icon).
 */

import { Check, Download, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { DEFAULT_EXPORT_OPTIONS, type ExportOptions } from '../../lib/export-conversation'

// ============================================================================
// Types
// ============================================================================

type ExportFormat = 'markdown' | 'json' | 'plaintext'

interface FormatOption {
  value: ExportFormat
  label: string
}

const FORMAT_OPTIONS: FormatOption[] = [
  { value: 'markdown', label: 'Markdown (.md)' },
  { value: 'json', label: 'JSON (.json)' },
  { value: 'plaintext', label: 'Plain Text (.txt)' },
]

interface ExportOptionsDialogProps {
  open: boolean
  onClose: () => void
  onExport: (options: ExportOptions) => void
}

// ============================================================================
// Component
// ============================================================================

export const ExportOptionsDialog: Component<ExportOptionsDialogProps> = (props) => {
  const [format, setFormat] = createSignal<ExportFormat>('markdown')
  const [includeToolOutputs, setIncludeToolOutputs] = createSignal(true)
  const [includeThinking, setIncludeThinking] = createSignal(true)
  const [includeSystemMessages, setIncludeSystemMessages] = createSignal(false)

  const handleExport = () => {
    props.onExport({
      redaction: {
        stripApiKeys: DEFAULT_EXPORT_OPTIONS.redaction.stripApiKeys,
        stripFilePaths: DEFAULT_EXPORT_OPTIONS.redaction.stripFilePaths,
        stripEmails: DEFAULT_EXPORT_OPTIONS.redaction.stripEmails,
      },
      includeMetadata: includeSystemMessages(),
      includeArtifacts: includeToolOutputs(),
    })
    props.onClose()
  }

  return (
    <Show when={props.open}>
      {/* Backdrop */}
      {/* biome-ignore lint/a11y/useKeyWithClickEvents: backdrop dismiss */}
      {/* biome-ignore lint/a11y/noStaticElementInteractions: backdrop dismiss */}
      <div
        class="fixed inset-0 z-50 flex items-center justify-center"
        style={{ background: 'rgba(0, 0, 0, 0.6)' }}
        onClick={(e) => {
          if (e.target === e.currentTarget) props.onClose()
        }}
      >
        {/* Dialog card */}
        <div
          style={{
            width: '440px',
            'max-width': 'calc(100% - 32px)',
            'border-radius': '12px',
            background: 'var(--surface)',
            border: '1px solid var(--border-default)',
            'box-shadow': '0 12px 24px rgba(0, 0, 0, 0.4)',
            overflow: 'hidden',
            display: 'flex',
            'flex-direction': 'column',
          }}
        >
          {/* Header */}
          <div
            class="flex items-center justify-between"
            style={{
              height: '48px',
              padding: '0 16px',
              background: 'var(--background-subtle)',
            }}
          >
            <div class="flex items-center gap-2.5" style={{ height: '100%' }}>
              <Download class="w-4 h-4" style={{ color: 'var(--accent)' }} />
              <span class="text-[14px] font-semibold" style={{ color: 'var(--text-primary)' }}>
                Export Conversation
              </span>
            </div>
            <button
              type="button"
              onClick={() => props.onClose()}
              class="flex items-center justify-center transition-colors"
              style={{
                background: 'none',
                border: 'none',
                cursor: 'pointer',
                color: 'var(--text-muted)',
                padding: '4px',
              }}
            >
              <X class="w-4 h-4" />
            </button>
          </div>

          {/* Body */}
          <div
            style={{
              padding: '20px',
              display: 'flex',
              'flex-direction': 'column',
              gap: '16px',
            }}
          >
            {/* Format section */}
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '8px' }}>
              <span
                style={{
                  color: 'var(--text-muted)',
                  'font-size': '11px',
                  'font-weight': '500',
                  'letter-spacing': '0.5px',
                  'text-transform': 'uppercase',
                }}
              >
                Format
              </span>

              <div style={{ display: 'flex', 'flex-direction': 'column', gap: '6px' }}>
                <For each={FORMAT_OPTIONS}>
                  {(opt) => {
                    const isSelected = () => format() === opt.value
                    return (
                      <button
                        type="button"
                        onClick={() => setFormat(opt.value)}
                        class="flex items-center transition-colors"
                        style={{
                          width: '100%',
                          height: '40px',
                          padding: '0 12px',
                          gap: '10px',
                          'border-radius': '6px',
                          background: 'var(--background-subtle)',
                          border: `1px solid ${isSelected() ? 'var(--accent-border)' : 'var(--border-subtle)'}`,
                          cursor: 'pointer',
                          'text-align': 'left',
                        }}
                      >
                        {/* Radio circle */}
                        <div
                          style={{
                            width: '14px',
                            height: '14px',
                            'border-radius': '50%',
                            border: `${isSelected() ? '2px' : '1.5px'} solid ${isSelected() ? 'var(--accent)' : 'var(--text-muted)'}`,
                            display: 'flex',
                            'align-items': 'center',
                            'justify-content': 'center',
                            'flex-shrink': '0',
                          }}
                        >
                          <Show when={isSelected()}>
                            <div
                              style={{
                                width: '6px',
                                height: '6px',
                                'border-radius': '50%',
                                background: 'var(--accent)',
                              }}
                            />
                          </Show>
                        </div>
                        <span
                          style={{
                            color: isSelected() ? 'var(--text-primary)' : 'var(--text-secondary)',
                            'font-size': '12px',
                          }}
                        >
                          {opt.label}
                        </span>
                      </button>
                    )
                  }}
                </For>
              </div>
            </div>

            {/* Include section */}
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '8px' }}>
              <span
                style={{
                  color: 'var(--text-muted)',
                  'font-size': '11px',
                  'font-weight': '500',
                  'letter-spacing': '0.5px',
                  'text-transform': 'uppercase',
                }}
              >
                Include
              </span>

              <div style={{ display: 'flex', 'flex-direction': 'column', gap: '8px' }}>
                {/* Tool call outputs — checked */}
                <CheckboxRow
                  checked={includeToolOutputs()}
                  onChange={setIncludeToolOutputs}
                  label="Tool call outputs"
                />

                {/* Thinking blocks — checked */}
                <CheckboxRow
                  checked={includeThinking()}
                  onChange={setIncludeThinking}
                  label="Thinking blocks"
                />

                {/* System messages — unchecked */}
                <CheckboxRow
                  checked={includeSystemMessages()}
                  onChange={setIncludeSystemMessages}
                  label="System messages"
                />
              </div>
            </div>
          </div>

          {/* Footer */}
          <div
            class="flex items-center justify-end gap-2"
            style={{
              padding: '12px 20px 16px 20px',
            }}
          >
            {/* Cancel — ghost */}
            <button
              type="button"
              onClick={() => props.onClose()}
              class="inline-flex items-center justify-center transition-colors"
              style={{
                padding: '8px 16px',
                'border-radius': '6px',
                background: 'rgba(255, 255, 255, 0.024)',
                border: '1px solid var(--border-default)',
                color: 'var(--text-secondary)',
                'font-size': '12px',
                'font-weight': '500',
                cursor: 'pointer',
              }}
            >
              Cancel
            </button>

            {/* Export — blue filled with icon */}
            <button
              type="button"
              onClick={handleExport}
              class="inline-flex items-center justify-center gap-1.5 transition-colors"
              style={{
                padding: '8px 20px',
                'border-radius': '6px',
                background: 'var(--accent)',
                color: 'white',
                'font-size': '12px',
                'font-weight': '600',
                cursor: 'pointer',
                border: 'none',
              }}
            >
              <Download class="w-[13px] h-[13px]" />
              Export
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}

// ============================================================================
// Checkbox Row (matches Pencil design)
// ============================================================================

const CheckboxRow: Component<{
  checked: boolean
  onChange: (v: boolean) => void
  label: string
}> = (props) => (
  <button
    type="button"
    onClick={() => props.onChange(!props.checked)}
    class="flex items-center"
    style={{
      gap: '10px',
      width: '100%',
      background: 'none',
      border: 'none',
      cursor: 'pointer',
      padding: '0',
      'text-align': 'left',
    }}
  >
    {/* Checkbox */}
    <div
      style={{
        width: '16px',
        height: '16px',
        'border-radius': '3px',
        background: props.checked ? 'var(--accent)' : 'transparent',
        border: props.checked ? 'none' : '1.5px solid var(--text-muted)',
        display: 'flex',
        'align-items': 'center',
        'justify-content': 'center',
        'flex-shrink': '0',
      }}
    >
      <Show when={props.checked}>
        <Check class="w-[10px] h-[10px]" style={{ color: 'white' }} />
      </Show>
    </div>
    <span style={{ color: 'var(--text-secondary)', 'font-size': '12px' }}>{props.label}</span>
  </button>
)
