/**
 * Custom Commands Service
 *
 * Reads/writes TOML command files via Tauri FS.
 * Inline parser logic matches packages/extensions/instructions/src/custom-commands/parser.ts.
 */

import { isTauri } from '@tauri-apps/api/core'

export interface CustomCommandFile {
  name: string
  description: string
  prompt: string
  allowedTools?: string[]
  mode?: string
  filePath: string
}

/** Lazy-load Tauri FS */
async function getFs() {
  if (!isTauri()) return null
  try {
    return await import('@tauri-apps/plugin-fs')
  } catch {
    return null
  }
}

/** Get commands directory, creating it if needed */
export async function getCommandsDir(): Promise<string> {
  const fs = await getFs()
  if (!fs) return ''
  const dir = '.config/ava/commands'
  const exists = await fs.exists(dir, { baseDir: fs.BaseDirectory.Home })
  if (!exists) {
    await fs.mkdir(dir, { baseDir: fs.BaseDirectory.Home, recursive: true })
  }
  return dir
}

/** Parse TOML content into a CustomCommandFile */
function parseToml(content: string, filePath: string): CustomCommandFile | null {
  const fields: Record<string, string> = {}
  let inMultiline = false
  let multilineKey = ''
  let multilineValue = ''

  for (const line of content.split('\n')) {
    const trimmed = line.trim()

    if (inMultiline) {
      if (trimmed === '"""' || trimmed === "'''") {
        fields[multilineKey] = multilineValue.trim()
        inMultiline = false
        continue
      }
      multilineValue += `${line}\n`
      continue
    }

    if (trimmed.startsWith('#') || trimmed === '') continue

    const eqIdx = trimmed.indexOf('=')
    if (eqIdx === -1) continue

    const key = trimmed.slice(0, eqIdx).trim()
    let value = trimmed.slice(eqIdx + 1).trim()

    if (value === '"""' || value === "'''") {
      inMultiline = true
      multilineKey = key
      multilineValue = ''
      continue
    }

    if (
      (value.startsWith('"') && value.endsWith('"')) ||
      (value.startsWith("'") && value.endsWith("'"))
    ) {
      value = value.slice(1, -1)
    }

    fields[key] = value
  }

  const name = fields.name
  const prompt = fields.prompt
  if (!name || !prompt) return null

  let allowedTools: string[] | undefined
  if (fields.allowed_tools) {
    allowedTools = fields.allowed_tools
      .replace(/^\[|\]$/g, '')
      .split(',')
      .map((t) => t.trim().replace(/^["']|["']$/g, ''))
      .filter(Boolean)
  }

  return {
    name,
    description: fields.description ?? '',
    prompt,
    allowedTools,
    mode: fields.mode,
    filePath,
  }
}

/** Serialize command to TOML string */
function serializeToToml(cmd: Omit<CustomCommandFile, 'filePath'>): string {
  const lines: string[] = []
  lines.push(`name = "${cmd.name}"`)
  if (cmd.description) {
    lines.push(`description = "${cmd.description}"`)
  }
  if (cmd.mode) {
    lines.push(`mode = "${cmd.mode}"`)
  }
  if (cmd.allowedTools && cmd.allowedTools.length > 0) {
    lines.push(`allowed_tools = [${cmd.allowedTools.map((t) => `"${t}"`).join(', ')}]`)
  }
  if (cmd.prompt.includes('\n')) {
    lines.push('prompt = """')
    lines.push(cmd.prompt)
    lines.push('"""')
  } else {
    lines.push(`prompt = "${cmd.prompt}"`)
  }
  return lines.join('\n')
}

/** List all custom commands from ~/.config/ava/commands/ */
export async function listCommands(): Promise<CustomCommandFile[]> {
  const fs = await getFs()
  if (!fs) return []
  const dir = await getCommandsDir()
  if (!dir) return []

  try {
    const entries = await fs.readDir(dir, { baseDir: fs.BaseDirectory.Home })
    const commands: CustomCommandFile[] = []

    for (const entry of entries) {
      if (!entry.name?.endsWith('.toml')) continue
      const filePath = `${dir}/${entry.name}`
      try {
        const content = await fs.readTextFile(filePath, { baseDir: fs.BaseDirectory.Home })
        const cmd = parseToml(content, filePath)
        if (cmd) commands.push(cmd)
      } catch {
        // skip unparseable files
      }
    }

    return commands.sort((a, b) => a.name.localeCompare(b.name))
  } catch {
    return []
  }
}

/** Save a command to a TOML file. Returns the file path. */
export async function saveCommand(
  cmd: Omit<CustomCommandFile, 'filePath'>,
  existingPath?: string
): Promise<string> {
  const fs = await getFs()
  if (!fs) throw new Error('Filesystem not available')
  const dir = await getCommandsDir()

  const fileName = existingPath || `${dir}/${cmd.name.replace(/\s+/g, '-').toLowerCase()}.toml`
  const content = serializeToToml(cmd)
  await fs.writeTextFile(fileName, content, { baseDir: fs.BaseDirectory.Home })
  return fileName
}

/** Delete a command file */
export async function deleteCommand(filePath: string): Promise<void> {
  const fs = await getFs()
  if (!fs) return
  await fs.remove(filePath, { baseDir: fs.BaseDirectory.Home })
}
