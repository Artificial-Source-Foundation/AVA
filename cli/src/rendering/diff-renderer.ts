import type { EventEmitter } from 'node:events'

interface RenderStream extends EventEmitter {
  write(chunk: string): boolean
  isTTY?: boolean
  columns?: number
}

export class DiffRenderer {
  private previousFrame: string[] = []
  private pendingFullRedraw = true
  private readonly supportsCursor: boolean

  constructor(private readonly stream: RenderStream = process.stdout as RenderStream) {
    this.supportsCursor = Boolean(this.stream.isTTY && typeof this.stream.columns === 'number')
    this.stream.on('resize', this.onResize)
  }

  /** Render a new frame, only outputting changed lines */
  render(lines: string[]): void {
    if (!this.supportsCursor) {
      this.stream.write(`${lines.join('\n')}\n`)
      this.previousFrame = [...lines]
      return
    }

    this.stream.write('\x1b[?25l')
    try {
      if (this.pendingFullRedraw) {
        this.stream.write('\x1b[H\x1b[2J')
      }

      const maxLines = Math.max(lines.length, this.previousFrame.length)
      for (let i = 0; i < maxLines; i += 1) {
        const nextLine = lines[i] ?? ''
        const prevLine = this.previousFrame[i] ?? ''
        if (!this.pendingFullRedraw && nextLine === prevLine) {
          continue
        }
        this.stream.write(`\x1b[${i + 1};1H\x1b[2K${nextLine}`)
      }

      this.stream.write(`\x1b[${lines.length + 1};1H`)
      this.previousFrame = [...lines]
      this.pendingFullRedraw = false
    } finally {
      this.stream.write('\x1b[?25h')
    }
  }

  /** Force full redraw */
  forceRedraw(): void {
    this.pendingFullRedraw = true
  }

  /** Clear screen and reset state */
  clear(): void {
    this.previousFrame = []
    this.pendingFullRedraw = true

    if (!this.supportsCursor) {
      return
    }

    this.stream.write('\x1b[2J\x1b[H')
  }

  dispose(): void {
    this.stream.removeListener('resize', this.onResize)
  }

  private readonly onResize = (): void => {
    this.pendingFullRedraw = true
  }
}
