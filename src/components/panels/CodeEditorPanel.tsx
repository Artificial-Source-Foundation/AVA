/**
 * Code Editor Panel
 *
 * VS Code-like code viewing panel using CodeMirror 6.
 * Shows files from the current session's file operations.
 * Read-only by default, uses One Dark theme with glass styling.
 */

import { javascript } from '@codemirror/lang-javascript'
import { json } from '@codemirror/lang-json'
import { oneDark } from '@codemirror/theme-one-dark'
import { EditorView } from '@codemirror/view'
import { Code2, FileText, X } from 'lucide-solid'
import {
  createCodeMirror,
  createEditorControlledValue,
  createEditorReadonly,
} from 'solid-codemirror'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'

// ============================================================================
// Language Detection
// ============================================================================

function getLanguageExtension(filePath: string) {
  const ext = filePath.split('.').pop()?.toLowerCase() ?? ''
  switch (ext) {
    case 'ts':
    case 'tsx':
    case 'mts':
    case 'cts':
      return javascript({ typescript: true, jsx: ext.includes('x') })
    case 'js':
    case 'jsx':
    case 'mjs':
    case 'cjs':
      return javascript({ jsx: ext.includes('x') })
    case 'json':
      return json()
    default:
      return javascript()
  }
}

// ============================================================================
// Custom Theme (transparent bg to show glass)
// ============================================================================

const estelaEditorTheme = EditorView.theme({
  '&': {
    backgroundColor: 'transparent',
    fontSize: '13px',
    height: '100%',
  },
  '.cm-content': {
    fontFamily: '"JetBrains Mono", "Fira Code", "Cascadia Code", monospace',
    caretColor: 'var(--accent)',
    padding: '8px 0',
  },
  '.cm-cursor': {
    borderLeftColor: 'var(--accent)',
  },
  '&.cm-focused .cm-selectionBackground, .cm-selectionBackground': {
    backgroundColor: 'rgba(255, 255, 255, 0.08) !important',
  },
  '.cm-gutters': {
    backgroundColor: 'transparent',
    borderRight: '1px solid var(--border-subtle)',
    color: 'var(--text-muted)',
    paddingLeft: '4px',
  },
  '.cm-activeLineGutter': {
    backgroundColor: 'rgba(255, 255, 255, 0.03)',
    color: 'var(--text-secondary)',
  },
  '.cm-activeLine': {
    backgroundColor: 'rgba(255, 255, 255, 0.03)',
  },
  '.cm-scroller': {
    overflow: 'auto',
  },
})

// ============================================================================
// Helpers
// ============================================================================

function getFileName(path: string): string {
  return path.split('/').pop() || path
}

function getFileExt(path: string): string {
  const name = getFileName(path)
  const dot = name.lastIndexOf('.')
  return dot >= 0 ? name.slice(dot + 1) : ''
}

// ============================================================================
// Component
// ============================================================================

export const CodeEditorPanel: Component = () => {
  const { fileOperations } = useSession()
  const [selectedFilePath, setSelectedFilePath] = createSignal<string | null>(null)
  const [content, setContent] = createSignal('')

  // Deduplicate files by path, keep latest operation first
  const uniqueFiles = createMemo(() => {
    const seen = new Set<string>()
    return fileOperations()
      .filter((op) => {
        if (seen.has(op.filePath)) return false
        seen.add(op.filePath)
        return true
      })
      .slice(0, 20)
  })

  // Selected file's most recent operation
  const selectedFile = createMemo(() => {
    const path = selectedFilePath()
    if (!path) return null
    return fileOperations().find((op) => op.filePath === path) ?? null
  })

  // Reactive language extension based on selected file
  const languageExtension = createMemo(() => {
    const path = selectedFilePath()
    return path ? getLanguageExtension(path) : javascript()
  })

  // CodeMirror instance
  /* eslint-disable solid/reactivity -- CodeMirror reads initial value once */
  const { ref, editorView, createExtension } = createCodeMirror({
    value: content(),
    onValueChange: setContent,
  })
  /* eslint-enable solid/reactivity */

  // Extensions
  createExtension(() => oneDark)
  createExtension(() => estelaEditorTheme)
  createExtension(() => languageExtension())
  createExtension(() => EditorView.lineWrapping)

  // Read-only + controlled value
  createEditorReadonly(editorView, () => true)
  createEditorControlledValue(editorView, content)

  // Select a file tab
  const handleSelectFile = (filePath: string) => {
    setSelectedFilePath(filePath)
    // TODO: Read actual file content via Tauri fs commands
    const fileName = getFileName(filePath)
    setContent(
      `// ${fileName}\n// Path: ${filePath}\n//\n// File content will be loaded when connected\n// to the Tauri file system backend.\n`
    )
  }

  // Close active file tab
  const handleCloseFile = (e: MouseEvent) => {
    e.stopPropagation()
    setSelectedFilePath(null)
    setContent('')
  }

  return (
    <div class="flex flex-col h-full">
      {/* File Tabs */}
      <Show when={uniqueFiles().length > 0}>
        <div class="flex items-center gap-0 border-b border-[var(--border-subtle)] bg-[var(--surface-sunken)] overflow-x-auto scrollbar-none">
          <For each={uniqueFiles()}>
            {(op) => {
              const isActive = () => selectedFilePath() === op.filePath

              return (
                <button
                  type="button"
                  onClick={() => handleSelectFile(op.filePath)}
                  class={`
                    group flex items-center gap-1.5 px-3 py-2
                    text-xs font-medium whitespace-nowrap
                    border-r border-[var(--border-subtle)]
                    transition-colors
                    ${
                      isActive()
                        ? 'bg-[var(--surface)] text-[var(--text-primary)] shadow-[inset_0_-2px_0_var(--accent)]'
                        : 'text-[var(--text-tertiary)] hover:text-[var(--text-secondary)] hover:bg-[var(--surface-raised)]'
                    }
                  `}
                >
                  <FileText class="w-3.5 h-3.5 flex-shrink-0" />
                  <span>{getFileName(op.filePath)}</span>
                  <Show when={isActive()}>
                    <button
                      type="button"
                      class="ml-1 p-0.5 rounded hover:bg-[var(--alpha-white-10)]"
                      onClick={handleCloseFile}
                    >
                      <X class="w-3 h-3" />
                    </button>
                  </Show>
                </button>
              )
            }}
          </For>
        </div>
      </Show>

      {/* Editor or Empty State */}
      <Show
        when={selectedFilePath()}
        fallback={
          <div class="flex-1 flex items-center justify-center">
            <div class="text-center p-8">
              <div class="p-4 bg-[var(--surface-raised)] rounded-full mb-4 inline-flex">
                <Code2 class="w-8 h-8 text-[var(--text-muted)]" />
              </div>
              <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-1">
                {uniqueFiles().length > 0 ? 'Select a file to view' : 'No files yet'}
              </h3>
              <p class="text-xs text-[var(--text-muted)] max-w-[240px] mx-auto">
                {uniqueFiles().length > 0
                  ? 'Click on a file tab above to view its contents'
                  : 'File operations from agent activities will appear here'}
              </p>
            </div>
          </div>
        }
      >
        {/* File Info Bar */}
        <div class="flex items-center gap-2 px-3 py-1.5 text-xs text-[var(--text-muted)] bg-[var(--surface-sunken)] border-b border-[var(--border-subtle)]">
          <span class="font-mono truncate">{selectedFilePath()}</span>
          <Show when={selectedFile()}>
            <span class="ml-auto flex items-center gap-2 flex-shrink-0">
              <span class="uppercase text-[10px] font-semibold px-1.5 py-0.5 rounded bg-[var(--surface-raised)]">
                {getFileExt(selectedFilePath()!)}
              </span>
              <span class="text-[var(--text-muted)]">Read-only</span>
            </span>
          </Show>
        </div>

        {/* CodeMirror Editor */}
        <div class="flex-1 overflow-hidden" ref={ref} />
      </Show>
    </div>
  )
}
