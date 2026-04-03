declare module '@xterm/xterm' {
  export interface TerminalOptions {
    theme?: Record<string, string>
    fontFamily?: string
    fontSize?: number
    lineHeight?: number
    cursorBlink?: boolean
    cursorStyle?: string
    scrollback?: number
    allowProposedApi?: boolean
  }

  export interface IDisposable {
    dispose(): void
  }

  export interface IResizeEvent {
    cols: number
    rows: number
  }

  export interface ITerminalAddon {
    activate(terminal: Terminal): void
    dispose(): void
  }

  export class Terminal {
    cols: number
    rows: number
    constructor(options?: TerminalOptions)
    loadAddon(addon: ITerminalAddon): void
    open(element: HTMLElement): void
    write(data: string): void
    onData(callback: (data: string) => void): IDisposable
    onResize(callback: (event: IResizeEvent) => void): IDisposable
    attachCustomKeyEventHandler(handler: (event: KeyboardEvent) => boolean): void
    dispose(): void
  }
}

declare module '@xterm/addon-fit' {
  import type { ITerminalAddon } from '@xterm/xterm'

  export class FitAddon implements ITerminalAddon {
    activate(): void
    dispose(): void
    fit(): void
  }
}

declare module '@xterm/addon-web-links' {
  import type { ITerminalAddon } from '@xterm/xterm'

  export class WebLinksAddon implements ITerminalAddon {
    activate(): void
    dispose(): void
  }
}

declare module '@xterm/addon-webgl' {
  import type { ITerminalAddon } from '@xterm/xterm'

  export class WebglAddon implements ITerminalAddon {
    activate(): void
    dispose(): void
  }
}
