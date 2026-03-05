function toLinesWithEndings(text: string): string[] {
  if (text.length === 0) {
    return ['']
  }

  const lines = text.match(/.*(?:\r\n|\n|$)/g) ?? []
  if (lines.length > 0 && lines[lines.length - 1] === '') {
    lines.pop()
  }
  return lines
}

function leadingWhitespace(text: string): string {
  const match = text.match(/^[ \t]*/)
  return match?.[0] ?? ''
}

function lineWithoutEnding(line: string): string {
  return line.replace(/[\r\n]+$/g, '')
}

export class RelativeIndenter {
  private readonly marker: string

  constructor(texts: string[]) {
    const chars = new Set<string>()
    for (const text of texts) {
      for (const char of text) {
        chars.add(char)
      }
    }

    const fallback = '\u2190'
    if (!chars.has(fallback)) {
      this.marker = fallback
      return
    }

    for (let codepoint = 0x10ffff; codepoint >= 0x10000; codepoint -= 1) {
      const candidate = String.fromCodePoint(codepoint)
      if (!chars.has(candidate)) {
        this.marker = candidate
        return
      }
    }

    throw new Error('Could not find a unique outdent marker')
  }

  makeRelative(text: string): string {
    if (text.includes(this.marker)) {
      throw new Error('Input already contains RelativeIndenter marker')
    }

    const output: string[] = []
    let previousIndent = ''

    for (const line of toLinesWithEndings(text)) {
      const content = lineWithoutEnding(line)
      const indent = leadingWhitespace(content)
      const change = indent.length - previousIndent.length

      let relativeIndent = ''
      if (change > 0) {
        relativeIndent = indent.slice(-change)
      } else if (change < 0) {
        relativeIndent = this.marker.repeat(Math.abs(change))
      }

      output.push(`${relativeIndent}\n${line.slice(indent.length)}`)
      previousIndent = indent
    }

    return output.join('')
  }

  makeAbsolute(text: string): string {
    const lines = toLinesWithEndings(text)
    const output: string[] = []
    let previousIndent = ''

    for (let i = 0; i < lines.length; i += 2) {
      const indentLine = lineWithoutEnding(lines[i] ?? '')
      const contentLine = lines[i + 1] ?? ''

      let currentIndent: string
      if (indentLine.startsWith(this.marker)) {
        currentIndent = previousIndent.slice(
          0,
          Math.max(0, previousIndent.length - indentLine.length)
        )
      } else {
        currentIndent = previousIndent + indentLine
      }

      const contentWithoutEnding = lineWithoutEnding(contentLine)
      if (contentWithoutEnding.trim().length === 0) {
        output.push(contentLine)
      } else {
        output.push(`${currentIndent}${contentLine}`)
      }

      previousIndent = currentIndent
    }

    const restored = output.join('')
    if (restored.includes(this.marker)) {
      throw new Error('RelativeIndenter marker leaked into absolute text')
    }

    return restored
  }
}
