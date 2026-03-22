/**
 * Command Edit Form
 *
 * Form for creating/editing custom TOML command files.
 */

import { type Component, createSignal, Show } from 'solid-js'
import type { CustomCommandFile } from '../../../../services/custom-commands'
import { SETTINGS_FORM_INPUT_CLASS } from '../../settings-constants'

export interface EditFormProps {
  initial?: CustomCommandFile
  onSave: (cmd: Omit<CustomCommandFile, 'filePath'>, existingPath?: string) => void
  onCancel: () => void
}

export const CommandEditForm: Component<EditFormProps> = (props) => {
  const [name, setName] = createSignal(props.initial?.name ?? '')
  const [description, setDescription] = createSignal(props.initial?.description ?? '')
  const [prompt, setPrompt] = createSignal(props.initial?.prompt ?? '')
  const [allowedTools, setAllowedTools] = createSignal(
    props.initial?.allowedTools?.join(', ') ?? ''
  )
  const [mode, setMode] = createSignal(props.initial?.mode ?? '')
  const [nameError, setNameError] = createSignal('')

  const validate = (): boolean => {
    if (!name().trim()) {
      setNameError('Name is required')
      return false
    }
    if (/\s/.test(name().trim())) {
      setNameError('Name cannot contain spaces')
      return false
    }
    if (!prompt().trim()) return false
    setNameError('')
    return true
  }

  const handleSave = () => {
    if (!validate()) return
    const tools = allowedTools()
      .split(',')
      .map((t) => t.trim())
      .filter(Boolean)
    props.onSave(
      {
        name: name().trim(),
        description: description().trim(),
        prompt: prompt(),
        allowedTools: tools.length > 0 ? tools : undefined,
        mode: mode() || undefined,
      },
      props.initial?.filePath
    )
  }

  const inputClass = SETTINGS_FORM_INPUT_CLASS

  return (
    <div class="space-y-3">
      <div>
        <label class="block">
          <span class="text-[var(--settings-text-badge)] font-medium text-[var(--text-muted)] uppercase tracking-wider">
            Name *
          </span>
          <input
            type="text"
            value={name()}
            onInput={(e) => {
              setName(e.currentTarget.value)
              setNameError('')
            }}
            class={inputClass}
            placeholder="my-command"
          />
        </label>
        <Show when={nameError()}>
          <p class="text-[var(--settings-text-badge)] text-[var(--error)] mt-0.5">{nameError()}</p>
        </Show>
      </div>

      <label class="block">
        <span class="text-[var(--settings-text-badge)] font-medium text-[var(--text-muted)] uppercase tracking-wider">
          Description
        </span>
        <input
          type="text"
          value={description()}
          onInput={(e) => setDescription(e.currentTarget.value)}
          class={inputClass}
          placeholder="What this command does"
        />
      </label>

      <label class="block">
        <span class="text-[var(--settings-text-badge)] font-medium text-[var(--text-muted)] uppercase tracking-wider">
          Prompt *
        </span>
        <textarea
          value={prompt()}
          onInput={(e) => setPrompt(e.currentTarget.value)}
          class={`${inputClass} font-mono min-h-[100px] resize-y`}
          placeholder="The prompt sent to the AI when this command runs..."
        />
      </label>

      <label class="block">
        <span class="text-[var(--settings-text-badge)] font-medium text-[var(--text-muted)] uppercase tracking-wider">
          Allowed Tools
        </span>
        <input
          type="text"
          value={allowedTools()}
          onInput={(e) => setAllowedTools(e.currentTarget.value)}
          class={inputClass}
          placeholder="bash, read_file, write_file"
        />
        <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)] mt-0.5">
          Comma-separated. Leave blank for all tools.
        </p>
      </label>

      <label class="block">
        <span class="text-[var(--settings-text-badge)] font-medium text-[var(--text-muted)] uppercase tracking-wider">
          Mode
        </span>
        <select value={mode()} onChange={(e) => setMode(e.currentTarget.value)} class={inputClass}>
          <option value="">Normal</option>
          <option value="plan">Plan</option>
        </select>
      </label>

      <div class="flex gap-2 pt-1">
        <button
          type="button"
          onClick={handleSave}
          disabled={!name().trim() || !prompt().trim()}
          class="px-3 py-1.5 text-xs font-medium rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:bg-[var(--accent-hover)] transition-colors disabled:opacity-40 disabled:cursor-not-allowed"
        >
          Save
        </button>
        <button
          type="button"
          onClick={props.onCancel}
          class="px-3 py-1.5 text-xs font-medium rounded-[var(--radius-md)] text-[var(--text-secondary)] hover:bg-[var(--surface-raised)] transition-colors"
        >
          Cancel
        </button>
      </div>
    </div>
  )
}
