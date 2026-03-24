/**
 * useSlashState Hook
 *
 * Encapsulates slash command detection, filtering, popover state,
 * and selection logic for the MessageInput text area.
 */

import { type Accessor, createMemo, createSignal } from 'solid-js'
import { type CommandEntry, getAvailableCommands } from '../../../services/command-resolver'

// ---------------------------------------------------------------------------
// Return type
// ---------------------------------------------------------------------------

export interface SlashState {
  slashOpen: Accessor<boolean>
  slashCommands: Accessor<CommandEntry[]>
  slashIndex: Accessor<number>
  setSlashIndex: (updater: number | ((prev: number) => number)) => void
  setSlashOpen: (v: boolean) => void
  handleSlashSelect: (
    cmd: CommandEntry,
    input: Accessor<string>,
    setInput: (v: string) => void,
    textareaRef: HTMLTextAreaElement | undefined
  ) => void
  checkSlash: (value: string, cursorPos: number) => void
}

// ---------------------------------------------------------------------------
// Hook
// ---------------------------------------------------------------------------

export function useSlashState(): SlashState {
  const [slashOpen, setSlashOpen] = createSignal(false)
  const [slashQuery, setSlashQuery] = createSignal('')
  const [slashIndex, setSlashIndex] = createSignal(0)

  const slashCommands = createMemo(() => {
    if (!slashOpen()) return []
    const q = slashQuery().toLowerCase()
    const all = getAvailableCommands()
    if (!q) return all
    return all.filter((cmd) => cmd.name.toLowerCase().startsWith(q))
  })

  /**
   * Detect `/` at the start of input.
   * Only triggers when the entire input is a slash command (starts with `/`).
   */
  const checkSlash = (value: string, _cursorPos: number): void => {
    // Only match when input starts with `/` and has no spaces yet
    // (once a space appears, the user is typing args, not browsing commands)
    const match = value.match(/^\/([a-zA-Z][\w-]*)?$/)
    if (match) {
      setSlashOpen(true)
      setSlashQuery(match[1] ?? '')
      setSlashIndex(0)
    } else {
      setSlashOpen(false)
    }
  }

  const handleSlashSelect = (
    cmd: CommandEntry,
    _input: Accessor<string>,
    setInput: (v: string) => void,
    textareaRef: HTMLTextAreaElement | undefined
  ): void => {
    const text = `/${cmd.name} `
    setInput(text)
    setSlashOpen(false)
    textareaRef?.focus()
    const pos = text.length
    queueMicrotask(() => textareaRef?.setSelectionRange(pos, pos))
  }

  return {
    slashOpen,
    slashCommands,
    slashIndex,
    setSlashIndex,
    setSlashOpen,
    handleSlashSelect,
    checkSlash,
  }
}
