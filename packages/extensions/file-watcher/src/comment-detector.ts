export interface AvaCommentDirective {
  marker: '// AVA:' | '# AVA:'
  message: string
  line: number
}

const DIRECTIVE_RE = /^(\s*)(\/\/\s*AVA:|#\s*AVA:)\s*(.*)$/

export function extractAvaCommentDirectives(content: string): AvaCommentDirective[] {
  const lines = content.split('\n')
  const directives: AvaCommentDirective[] = []

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    if (line === undefined) continue
    const match = DIRECTIVE_RE.exec(line)
    if (!match) continue

    const prefix = match[2] ?? ''
    const message = (match[3] ?? '').trim()
    directives.push({
      marker: prefix.startsWith('//') ? '// AVA:' : '# AVA:',
      message,
      line: i + 1,
    })
  }

  return directives
}

export function directiveSignature(path: string, directive: AvaCommentDirective): string {
  return `${path}:${directive.line}:${directive.marker}:${directive.message}`
}
