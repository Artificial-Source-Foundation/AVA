/**
 * Browser Session Manager — manages Puppeteer browser instances and pages.
 *
 * Copied from packages/core/src/tools/browser/session.ts.
 * Self-contained: defines its own minimal Puppeteer interfaces.
 */

interface PuppeteerBrowser {
  newPage(): Promise<PuppeteerPage>
  close(): Promise<void>
}

interface ConsoleMessage {
  type(): string
  text(): string
}

interface PageError {
  message: string
}

interface PuppeteerMouse {
  move(x: number, y: number): Promise<void>
  click(x: number, y: number): Promise<void>
}

interface PuppeteerKeyboard {
  type(text: string, options?: { delay?: number }): Promise<void>
}

interface PuppeteerPage {
  setViewport(viewport: { width: number; height: number }): Promise<void>
  on(event: 'console', handler: (msg: ConsoleMessage) => void): void
  on(event: 'pageerror', handler: (err: PageError) => void): void
  goto(url: string, options?: { waitUntil?: string; timeout?: number }): Promise<void>
  url(): string
  screenshot(options: { type: string; quality: number; encoding: string }): Promise<string>
  close(): Promise<void>
  mouse: PuppeteerMouse
  keyboard: PuppeteerKeyboard
  // biome-ignore lint/suspicious/noExplicitAny: Puppeteer's evaluate API requires any for serializable values
  evaluate<T>(fn: (...args: any[]) => T, ...args: any[]): Promise<T>
}

export interface BrowserSessionConfig {
  headless?: boolean
  viewportWidth?: number
  viewportHeight?: number
  sessionTimeout?: number
  consoleTimeout?: number
}

export interface BrowserState {
  url: string
  mousePosition: { x: number; y: number }
  consoleLogs: string[]
  isReady: boolean
}

const DEFAULT_CONFIG: Required<BrowserSessionConfig> = {
  headless: true,
  viewportWidth: 900,
  viewportHeight: 600,
  sessionTimeout: 5 * 60 * 1000,
  consoleTimeout: 3000,
}

export class BrowserSession {
  private browser: PuppeteerBrowser | null = null
  private page: PuppeteerPage | null = null
  private config: Required<BrowserSessionConfig>
  private consoleLogs: string[] = []
  private mousePosition = { x: 0, y: 0 }
  private sessionTimeoutId: ReturnType<typeof setTimeout> | null = null

  constructor(config: BrowserSessionConfig = {}) {
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  static async isAvailable(): Promise<boolean> {
    try {
      const moduleName = 'puppeteer'
      await import(/* @vite-ignore */ moduleName)
      return true
    } catch {
      return false
    }
  }

  async launch(url?: string): Promise<BrowserState> {
    if (!(await BrowserSession.isAvailable())) {
      throw new Error('Puppeteer is not installed. Install it with: npm install puppeteer')
    }

    const moduleName = 'puppeteer'
    const puppeteer = await import(/* @vite-ignore */ moduleName)

    if (!this.browser) {
      this.browser = (await puppeteer.default.launch({
        headless: this.config.headless,
        args: [
          '--no-sandbox',
          '--disable-setuid-sandbox',
          '--disable-dev-shm-usage',
          '--disable-accelerated-2d-canvas',
          '--disable-gpu',
        ],
      })) as unknown as PuppeteerBrowser
    }

    if (!this.page) {
      this.page = await this.browser.newPage()
      await this.page.setViewport({
        width: this.config.viewportWidth,
        height: this.config.viewportHeight,
      })

      this.page.on('console', (msg: ConsoleMessage) => {
        const text = `[${msg.type()}] ${msg.text()}`
        this.consoleLogs.push(text)
        if (this.consoleLogs.length > 100) {
          this.consoleLogs.shift()
        }
      })

      this.page.on('pageerror', (err: PageError) => {
        this.consoleLogs.push(`[error] ${err.message}`)
      })
    }

    if (url) {
      await this.page.goto(url, { waitUntil: 'networkidle2', timeout: 30000 })
      this.consoleLogs = []
    }

    this.resetSessionTimeout()
    return this.getState()
  }

  getState(): BrowserState {
    return {
      url: this.page?.url() ?? '',
      mousePosition: { ...this.mousePosition },
      consoleLogs: [...this.consoleLogs],
      isReady: this.page !== null,
    }
  }

  getPage(): PuppeteerPage | null {
    this.resetSessionTimeout()
    return this.page
  }

  setMousePosition(x: number, y: number): void {
    this.mousePosition = { x, y }
  }

  getConsoleLogs(clear = false): string[] {
    const logs = [...this.consoleLogs]
    if (clear) {
      this.consoleLogs = []
    }
    return logs
  }

  async waitForConsoleLogs(): Promise<string[]> {
    await new Promise((resolve) => setTimeout(resolve, this.config.consoleTimeout))
    return this.getConsoleLogs()
  }

  async takeScreenshot(): Promise<string> {
    if (!this.page) {
      throw new Error('No active page. Call launch() first.')
    }
    this.resetSessionTimeout()

    const buffer = await this.page.screenshot({
      type: 'webp',
      quality: 80,
      encoding: 'base64',
    })

    return `data:image/webp;base64,${buffer}`
  }

  async close(): Promise<void> {
    this.clearSessionTimeout()

    if (this.page) {
      await this.page.close().catch(() => {})
      this.page = null
    }

    if (this.browser) {
      await this.browser.close().catch(() => {})
      this.browser = null
    }

    this.consoleLogs = []
    this.mousePosition = { x: 0, y: 0 }
  }

  isActive(): boolean {
    return this.browser !== null && this.page !== null
  }

  private resetSessionTimeout(): void {
    this.clearSessionTimeout()
    this.sessionTimeoutId = setTimeout(() => {
      this.close()
    }, this.config.sessionTimeout)
  }

  private clearSessionTimeout(): void {
    if (this.sessionTimeoutId) {
      clearTimeout(this.sessionTimeoutId)
      this.sessionTimeoutId = null
    }
  }
}

const sessions = new Map<string, BrowserSession>()

export function getSession(sessionId: string, config?: BrowserSessionConfig): BrowserSession {
  let session = sessions.get(sessionId)
  if (!session) {
    session = new BrowserSession(config)
    sessions.set(sessionId, session)
  }
  return session
}

export async function closeSession(sessionId: string): Promise<void> {
  const session = sessions.get(sessionId)
  if (session) {
    await session.close()
    sessions.delete(sessionId)
  }
}

export async function closeAllSessions(): Promise<void> {
  const closePromises = Array.from(sessions.values()).map((s) => s.close())
  await Promise.all(closePromises)
  sessions.clear()
}

export function getActiveSessions(): string[] {
  return Array.from(sessions.entries())
    .filter(([, session]) => session.isActive())
    .map(([id]) => id)
}
