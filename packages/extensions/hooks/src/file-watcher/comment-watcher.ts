import { readFile } from 'node:fs/promises'

export interface CommentTrigger {
  file: string
  line: number
  comment: string
  surrounding: string
}

const DEFAULT_PATTERNS = [
  String.raw`^\s*//\s*ava:\s*(.+)$`,
  String.raw`^\s*#\s*ava:\s*(.+)$`,
  String.raw`^\s*/\*\s*ava:\s*(.+?)\s*\*/\s*$`,
  String.raw`^\s*//\s*TODO\(ava\):\s*(.+)$`,
  String.raw`^\s*//\s*FIXME\(ava\):\s*(.+)$`,
]

function buildComment(matchPrefix: string, body: string): string {
  return `${matchPrefix}: ${body.trim()}`
}

export class CommentWatcher {
  private patterns: RegExp[]

  constructor(patterns?: string[]) {
    this.patterns = (patterns && patterns.length > 0 ? patterns : DEFAULT_PATTERNS).map(
      (pattern) => new RegExp(pattern, 'i')
    )
  }

  /** Scan a file for trigger comments */
  scan(filePath: string, content: string): CommentTrigger[] {
    const lines = content.split('\n')
    const triggers: CommentTrigger[] = []

    for (let i = 0; i < lines.length; i += 1) {
      const currentLine = lines[i] ?? ''

      for (const pattern of this.patterns) {
        const match = pattern.exec(currentLine)
        if (!match) continue

        const body = (match[1] ?? '').trim()
        const lineNo = i + 1
        const from = Math.max(0, i - 2)
        const to = Math.min(lines.length, i + 3)
        const surrounding = lines.slice(from, to).join('\n')

        let prefix = 'ava'
        if (currentLine.toLowerCase().includes('todo(ava)')) prefix = 'TODO(ava)'
        if (currentLine.toLowerCase().includes('fixme(ava)')) prefix = 'FIXME(ava)'

        triggers.push({
          file: filePath,
          line: lineNo,
          comment: buildComment(prefix, body),
          surrounding,
        })
        break
      }
    }

    return triggers
  }

  /** Process a file change event */
  async onFileChanged(filePath: string): Promise<CommentTrigger[]> {
    const content = await readFile(filePath, 'utf8')
    return this.scan(filePath, content)
  }
}
