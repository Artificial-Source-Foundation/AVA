/**
 * Plugin Creation Wizard
 *
 * Multi-step wizard: choose template -> configure -> preview -> create.
 */

import { ArrowLeft, ArrowRight, Check, FileText, Puzzle, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import {
  generateIndexTs,
  generateManifest,
  kebabCase,
  TEMPLATES,
  type WizardStep,
} from './plugin-templates'

interface PluginWizardProps {
  open: boolean
  onClose: () => void
}

export const PluginWizard: Component<PluginWizardProps> = (props) => {
  const [step, setStep] = createSignal<WizardStep>('template')
  const [selectedTemplate, setSelectedTemplate] = createSignal<string | null>(null)
  const [pluginName, setPluginName] = createSignal('')
  const [pluginDescription, setPluginDescription] = createSignal('')
  const [pluginAuthor, setPluginAuthor] = createSignal('')
  const [created, setCreated] = createSignal(false)

  const template = () => TEMPLATES.find((t) => t.id === selectedTemplate())

  const reset = () => {
    setStep('template')
    setSelectedTemplate(null)
    setPluginName('')
    setPluginDescription('')
    setPluginAuthor('')
    setCreated(false)
  }

  const handleClose = () => {
    reset()
    props.onClose()
  }

  const handleCreate = () => {
    // In a real implementation, this would write files to disk via Tauri FS
    setCreated(true)
  }

  return (
    <Show when={props.open}>
      <div
        class="fixed inset-0 z-50 flex items-center justify-center"
        style={{ background: 'var(--modal-overlay)' }}
      >
        <div
          class="p-6 max-w-lg w-full"
          style={{
            background: 'var(--modal-surface)',
            border: '1px solid var(--modal-border)',
            'border-radius': 'var(--modal-radius-lg)',
            'box-shadow': 'var(--modal-shadow)',
          }}
        >
          {/* Header */}
          <div class="flex items-center justify-between mb-4">
            <div class="flex items-center gap-2">
              <Puzzle class="w-4 h-4 text-[var(--accent)]" />
              <h3 class="text-sm font-semibold text-[var(--text-primary)]">Create Plugin</h3>
              <Show when={step() !== 'template'}>
                <span class="text-[10px] text-[var(--text-muted)]">
                  — {step() === 'configure' ? 'Configure' : 'Preview'}
                </span>
              </Show>
            </div>
            <button
              type="button"
              onClick={handleClose}
              class="p-1 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-5)] transition-colors"
              style={{ color: 'var(--close-button-color)' }}
            >
              <X class="w-4 h-4" />
            </button>
          </div>

          {/* Step: Choose Template */}
          <Show when={step() === 'template'}>
            <div class="space-y-3">
              <p class="text-xs text-[var(--text-secondary)]">Choose a template to get started:</p>
              <div class="grid grid-cols-2 gap-2">
                <For each={TEMPLATES}>
                  {(tmpl) => {
                    const Icon = tmpl.icon
                    return (
                      <button
                        type="button"
                        onClick={() => {
                          setSelectedTemplate(tmpl.id)
                          setStep('configure')
                        }}
                        class={`text-left p-3 rounded-[var(--radius-lg)] border transition-colors ${
                          selectedTemplate() === tmpl.id
                            ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                            : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] bg-[var(--surface-raised)]'
                        }`}
                      >
                        <Icon class="w-4 h-4 text-[var(--accent)] mb-1.5" />
                        <p class="text-xs font-medium text-[var(--text-primary)]">{tmpl.name}</p>
                        <p class="text-[10px] text-[var(--text-muted)] mt-0.5 line-clamp-2">
                          {tmpl.description}
                        </p>
                      </button>
                    )
                  }}
                </For>
              </div>
            </div>
          </Show>

          {/* Step: Configure */}
          <Show when={step() === 'configure'}>
            <div class="space-y-3">
              <label class="block">
                <span class="text-[11px] text-[var(--text-secondary)] mb-1 block">Plugin Name</span>
                <input
                  type="text"
                  value={pluginName()}
                  onInput={(e) => setPluginName(e.currentTarget.value)}
                  placeholder="my-awesome-plugin"
                  class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                  autofocus
                />
              </label>
              <label class="block">
                <span class="text-[11px] text-[var(--text-secondary)] mb-1 block">Description</span>
                <textarea
                  value={pluginDescription()}
                  onInput={(e) => setPluginDescription(e.currentTarget.value)}
                  placeholder="What does this plugin do?"
                  rows={2}
                  class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none resize-none"
                />
              </label>
              <label class="block">
                <span class="text-[11px] text-[var(--text-secondary)] mb-1 block">
                  Author (optional)
                </span>
                <input
                  type="text"
                  value={pluginAuthor()}
                  onInput={(e) => setPluginAuthor(e.currentTarget.value)}
                  placeholder="Your name"
                  class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                />
              </label>
              <div class="flex gap-2 justify-between pt-2">
                <button
                  type="button"
                  onClick={() => setStep('template')}
                  class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors flex items-center gap-1"
                >
                  <ArrowLeft class="w-3 h-3" /> Back
                </button>
                <button
                  type="button"
                  onClick={() => setStep('preview')}
                  disabled={!pluginName().trim()}
                  class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:bg-[var(--accent-hover)] transition-colors disabled:opacity-50 flex items-center gap-1"
                >
                  Preview <ArrowRight class="w-3 h-3" />
                </button>
              </div>
            </div>
          </Show>

          {/* Step: Preview */}
          <Show when={step() === 'preview' && !created()}>
            <div class="space-y-3">
              <div class="flex items-center gap-2 mb-2">
                <FileText class="w-3.5 h-3.5 text-[var(--text-muted)]" />
                <span class="text-[11px] text-[var(--text-secondary)]">Generated Files</span>
              </div>

              {/* Manifest preview */}
              <div class="rounded-[var(--radius-md)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] overflow-hidden">
                <div class="px-3 py-1.5 bg-[var(--surface-raised)] border-b border-[var(--border-subtle)]">
                  <span class="text-[10px] text-[var(--text-muted)] font-mono">
                    ava-extension.json
                  </span>
                </div>
                <pre class="p-3 text-[10px] text-[var(--text-secondary)] font-mono overflow-x-auto max-h-24">
                  {generateManifest(template(), pluginName(), pluginDescription(), pluginAuthor())}
                </pre>
              </div>

              {/* Index.ts preview */}
              <div class="rounded-[var(--radius-md)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] overflow-hidden">
                <div class="px-3 py-1.5 bg-[var(--surface-raised)] border-b border-[var(--border-subtle)]">
                  <span class="text-[10px] text-[var(--text-muted)] font-mono">src/index.ts</span>
                </div>
                <pre class="p-3 text-[10px] text-[var(--text-secondary)] font-mono overflow-x-auto max-h-32">
                  {generateIndexTs(template(), pluginName(), pluginDescription())}
                </pre>
              </div>

              <div class="flex gap-2 justify-between pt-2">
                <button
                  type="button"
                  onClick={() => setStep('configure')}
                  class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors flex items-center gap-1"
                >
                  <ArrowLeft class="w-3 h-3" /> Back
                </button>
                <button
                  type="button"
                  onClick={handleCreate}
                  class="px-3 py-1.5 text-xs font-medium bg-[var(--success)] text-white rounded-[var(--radius-md)] hover:bg-[color-mix(in_srgb,var(--success)_88%,white_12%)] transition-colors flex items-center gap-1"
                >
                  <Check class="w-3 h-3" /> Create Plugin
                </button>
              </div>
            </div>
          </Show>

          {/* Created confirmation */}
          <Show when={created()}>
            <div class="text-center py-6 space-y-3">
              <div class="w-12 h-12 mx-auto rounded-full bg-[var(--success-subtle)] flex items-center justify-center">
                <Check class="w-6 h-6 text-[var(--success)]" />
              </div>
              <p class="text-sm font-medium text-[var(--text-primary)]">Plugin Created</p>
              <p class="text-xs text-[var(--text-muted)]">
                {kebabCase(pluginName())} has been scaffolded. Install it via the "Link Local"
                option.
              </p>
              <button
                type="button"
                onClick={handleClose}
                class="px-4 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:bg-[var(--accent-hover)] transition-colors"
              >
                Done
              </button>
            </div>
          </Show>
        </div>
      </div>
    </Show>
  )
}
