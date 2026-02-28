/**
 * IDE Integration Service
 *
 * Detects installed editors and opens files in the user's preferred IDE.
 * Uses Tauri shell plugin to check for editor CLIs.
 */

import { readTextFile, writeTextFile } from '@tauri-apps/plugin-fs'
import { Command } from '@tauri-apps/plugin-shell'

export interface EditorInfo {
  id: string
  name: string
  /** CLI command name */
  command: string
  available: boolean
}

const KNOWN_EDITORS: Omit<EditorInfo, 'available'>[] = [
  { id: 'vscode', name: 'VS Code', command: 'code' },
  { id: 'cursor', name: 'Cursor', command: 'cursor' },
  { id: 'zed', name: 'Zed', command: 'zed' },
  { id: 'windsurf', name: 'Windsurf', command: 'windsurf' },
  { id: 'neovim', name: 'Neovim', command: 'nvim' },
  { id: 'vim', name: 'Vim', command: 'vim' },
  { id: 'sublime', name: 'Sublime Text', command: 'subl' },
  { id: 'emacs', name: 'Emacs', command: 'emacs' },
]

let cachedEditors: EditorInfo[] | null = null

/** Detect which editors are installed on the system */
export async function detectEditors(): Promise<EditorInfo[]> {
  if (cachedEditors) return cachedEditors

  const results: EditorInfo[] = []

  for (const editor of KNOWN_EDITORS) {
    try {
      const cmd = Command.create('which', [editor.command])
      const output = await cmd.execute()
      results.push({ ...editor, available: output.code === 0 })
    } catch {
      results.push({ ...editor, available: false })
    }
  }

  cachedEditors = results
  return results
}

/** Get only the available (installed) editors */
export async function getAvailableEditors(): Promise<EditorInfo[]> {
  const all = await detectEditors()
  return all.filter((e) => e.available)
}

/** Open a file in a specific editor */
export async function openInEditor(
  editorCommand: string,
  filePath: string,
  lineNumber?: number
): Promise<boolean> {
  try {
    const args = lineNumber ? [`${filePath}:${lineNumber}`] : [filePath]

    // Some editors have special line number syntax
    if (editorCommand === 'code' || editorCommand === 'cursor' || editorCommand === 'windsurf') {
      const fileArg = lineNumber ? `${filePath}:${lineNumber}` : filePath
      const cmd = Command.create(editorCommand, ['--goto', fileArg])
      await cmd.execute()
    } else if (editorCommand === 'subl') {
      const cmd = Command.create(editorCommand, args)
      await cmd.execute()
    } else if (editorCommand === 'nvim' || editorCommand === 'vim' || editorCommand === 'emacs') {
      // Terminal editors — open in a new terminal window
      const lineArg = lineNumber ? `+${lineNumber}` : ''
      const termArgs = lineArg ? [editorCommand, lineArg, filePath] : [editorCommand, filePath]
      const cmd = Command.create(editorCommand, termArgs.slice(1))
      await cmd.execute()
    } else {
      const cmd = Command.create(editorCommand, args)
      await cmd.execute()
    }
    return true
  } catch {
    return false
  }
}

/** Open a directory/project in a specific editor */
export async function openProjectInEditor(
  editorCommand: string,
  dirPath: string
): Promise<boolean> {
  try {
    const cmd = Command.create(editorCommand, [dirPath])
    await cmd.execute()
    return true
  } catch {
    return false
  }
}

/** Reset the cached editors (e.g., after user installs a new editor) */
export function resetEditorCache(): void {
  cachedEditors = null
}

/**
 * Open text in an external terminal editor ($EDITOR / $VISUAL).
 * Writes text to a temp file, spawns the editor, waits for exit, reads back.
 * Returns the edited text. Falls back to nano, then vi if no env var is set.
 */
export async function openInExternalEditor(text: string): Promise<string> {
  const tmpPath = `/tmp/ava-prompt-${Date.now()}.md`
  await writeTextFile(tmpPath, text)

  // Determine editor: $VISUAL > $EDITOR > nano > vi
  let editorCmd = ''
  for (const envVar of ['VISUAL', 'EDITOR']) {
    try {
      const result = await Command.create('printenv', [envVar]).execute()
      const val = result.stdout.trim()
      if (val) {
        editorCmd = val
        break
      }
    } catch {
      // env var not set
    }
  }

  if (!editorCmd) {
    // Fallback: check for nano, then vi
    for (const fallback of ['nano', 'vi']) {
      try {
        const which = await Command.create('which', [fallback]).execute()
        if (which.code === 0) {
          editorCmd = fallback
          break
        }
      } catch {}
    }
  }

  if (!editorCmd) {
    throw new Error('No terminal editor found. Set $EDITOR or $VISUAL.')
  }

  // Open terminal with editor command
  // Use a wrapper script to open in a terminal emulator
  const script = `${editorCmd} "${tmpPath}"`
  const termCmd = Command.create('bash', ['-c', script])
  await termCmd.execute()

  const result = await readTextFile(tmpPath)
  // Clean up temp file
  try {
    await Command.create('rm', [tmpPath]).execute()
  } catch {
    // ignore cleanup failure
  }
  return result
}
