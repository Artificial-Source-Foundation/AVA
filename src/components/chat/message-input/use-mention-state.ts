/**
 * useMentionState Hook
 *
 * Encapsulates @ mention detection, file preloading, popover state,
 * and selection logic for the MessageInput text area.
 */

import { type Accessor, createEffect, createMemo, createSignal, on } from 'solid-js'
import type { SearchableFile } from '../../../services/file-search'
import { filterFiles, getProjectFiles } from '../../../services/file-search'
import { useProject } from '../../../stores/project'

// ---------------------------------------------------------------------------
// Return type
// ---------------------------------------------------------------------------

export interface MentionState {
  mentionOpen: Accessor<boolean>
  mentionFiltered: Accessor<SearchableFile[]>
  mentionIndex: Accessor<number>
  setMentionIndex: (updater: number | ((prev: number) => number)) => void
  setMentionOpen: (v: boolean) => void
  handleMentionSelect: (
    file: SearchableFile,
    input: Accessor<string>,
    setInput: (v: string) => void,
    textareaRef: HTMLTextAreaElement | undefined
  ) => void
  checkMention: (value: string, cursorPos: number) => void
}

// ---------------------------------------------------------------------------
// Hook
// ---------------------------------------------------------------------------

export function useMentionState(): MentionState {
  const { currentProject } = useProject()

  const [mentionOpen, setMentionOpen] = createSignal(false)
  const [mentionQuery, setMentionQuery] = createSignal('')
  const [mentionIndex, setMentionIndex] = createSignal(0)
  const [mentionStart, setMentionStart] = createSignal(-1)
  const [mentionFiles, setMentionFiles] = createSignal<SearchableFile[]>([])

  const mentionFiltered = createMemo(() =>
    mentionOpen() ? filterFiles(mentionFiles(), mentionQuery(), 12) : []
  )

  // Preload project files
  createEffect(
    on(
      () => currentProject()?.directory,
      async (dir) => {
        if (!dir) return
        setMentionFiles(await getProjectFiles(dir))
      }
    )
  )

  // @ mention detection
  const checkMention = (value: string, cursorPos: number): void => {
    let atPos = -1
    for (let i = cursorPos - 1; i >= 0; i--) {
      const ch = value[i]
      if (ch === '@') {
        if (i === 0 || /\s/.test(value[i - 1])) atPos = i
        break
      }
      if (/\s/.test(ch)) break
    }
    if (atPos >= 0) {
      setMentionOpen(true)
      setMentionQuery(value.slice(atPos + 1, cursorPos))
      setMentionStart(atPos)
      setMentionIndex(0)
    } else {
      setMentionOpen(false)
    }
  }

  const handleMentionSelect = (
    file: SearchableFile,
    input: Accessor<string>,
    setInput: (v: string) => void,
    textareaRef: HTMLTextAreaElement | undefined
  ): void => {
    const start = mentionStart()
    if (start < 0) return
    const before = input().slice(0, start)
    const after = input().slice(start + 1 + mentionQuery().length)
    const inserted = `@${file.relative} `
    setInput(before + inserted + after)
    setMentionOpen(false)
    textareaRef?.focus()
    const newPos = before.length + inserted.length
    queueMicrotask(() => textareaRef?.setSelectionRange(newPos, newPos))
  }

  return {
    mentionOpen,
    mentionFiltered,
    mentionIndex,
    setMentionIndex,
    setMentionOpen,
    handleMentionSelect,
    checkMention,
  }
}
