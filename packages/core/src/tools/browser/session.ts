/**
 * Browser Session Manager
 * Manages Puppeteer browser instances and pages
 *
 * Features:
 * - Lazy browser initialization
 * - Session reuse for efficiency
 * - Automatic cleanup on timeout
 * - Console log capture
 * - Screenshot capture in WebP format
 */

// Note: Puppeteer is an optional peer dependency
// This module uses dynamic imports to avoid errors when Puppeteer isn't installed
// We define minimal interfaces for the Puppeteer types we use

/** Minimal interface for Puppeteer Browser */
interface PuppeteerBrowser {
  newPage(): Promise<PuppeteerPage>
  close(): Promise<void>
}

/** Console message interface */
interface ConsoleMessage {
  type(): string
  text(): string
}

/** Page error interface */
interface PageError {
  message: string
}

/** Mouse interface */
interface PuppeteerMouse {
  move(x: number, y: number): Promise<void>
  click(x: number, y: number): Promise<void>
}

/** Keyboard interface */
interface PuppeteerKeyboard {
  type(text: string, options?: { delay?: number }): Promise<void>
}

/** Minimal interface for Puppeteer Page */
interface PuppeteerPage {
  setViewport(viewport: { width: number; height: number }): Promise<void>
  on(event: 'console', handler: (msg: ConsoleMessage) => void): void
  on(event: 'pageerror', handler: (err: PageError) => void): void
  goto(url: string, options?: { waitUntil?: string; timeout?: number }): Promise<void>
  url(): string
  screenshot(options: { type: string; quality: number; encoding: string }): Promise<string>
  close(): Promise<void>
  /** Mouse interface for click and move */
  mouse: PuppeteerMouse
  /** Keyboard interface for typing */
  keyboard: PuppeteerKeyboard
  /** Evaluate JavaScript in page context - uses any to match Puppeteer's flexible API */
  // biome-ignore lint/suspicious/noExplicitAny: Puppeteer's evaluate API requires any for serializable values
  evaluate<T>(fn: (...args: any[]) => T, ...args: any[]): Promise<T>
}

// ============================================================================
// Types
// ============================================================================

export interface BrowserSessionConfig {
  /** Run browser in headless mode (default: true) */
  headless?: boolean
  /** Viewport width (default: 900) */
  viewportWidth?: number
  /** Viewport height (default: 600) */
  viewportHeight?: number
  /** Session timeout in ms (default: 5 minutes) */
  sessionTimeout?: number
  /** Console log capture timeout in ms (default: 3000) */
  consoleTimeout?: number
}

export interface BrowserState {
  /** Current page URL */
  url: string
  /** Current mouse position */
  mousePosition: { x: number; y: number }
  /** Collected console logs */
  consoleLogs: string[]
  /** Whether browser is ready */
  isReady: boolean
}

const DEFAULT_CONFIG: Required<BrowserSessionConfig> = {
  headless: true,
  viewportWidth: 900,
  viewportHeight: 600,
  sessionTimeout: 5 * 60 * 1000, // 5 minutes
  consoleTimeout: 3000, // 3 seconds
}

// ============================================================================
// Browser Session
// ============================================================================

/**
 * Manages a single browser session
 */
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

  /**
   * Check if Puppeteer is available
   */
  static async isAvailable(): Promise<boolean> {
    try {
      // @ts-expect-error - puppeteer is an optional peer dependency
      await import('puppeteer')
      return true
    } catch {
      return false
    }
  }

  /**
   * Launch browser and create page
   */
  async launch(url?: string): Promise<BrowserState> {
    // Check Puppeteer availability
    if (!(await BrowserSession.isAvailable())) {
      throw new Error('Puppeteer is not installed. Install it with: npm install puppeteer')
    }

    // @ts-expect-error - puppeteer is an optional peer dependency
    const puppeteer = await import('puppeteer')

    // Launch browser if not already running
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

    // Create new page if not exists
    if (!this.page) {
      this.page = await this.browser.newPage()

      // Set viewport
      await this.page.setViewport({
        width: this.config.viewportWidth,
        height: this.config.viewportHeight,
      })

      // Set up console log capture
      this.page.on('console', (msg: ConsoleMessage) => {
        const text = `[${msg.type()}] ${msg.text()}`
        this.consoleLogs.push(text)
        // Keep only last 100 logs
        if (this.consoleLogs.length > 100) {
          this.consoleLogs.shift()
        }
      })

      // Set up error capture
      this.page.on('pageerror', (err: PageError) => {
        this.consoleLogs.push(`[error] ${err.message}`)
      })
    }

    // Navigate to URL if provided
    if (url) {
      await this.page.goto(url, { waitUntil: 'networkidle2', timeout: 30000 })
      this.consoleLogs = [] // Clear logs on new navigation
    }

    // Reset session timeout
    this.resetSessionTimeout()

    return this.getState()
  }

  /**
   * Get current browser state
   */
  getState(): BrowserState {
    return {
      url: this.page?.url() ?? '',
      mousePosition: { ...this.mousePosition },
      consoleLogs: [...this.consoleLogs],
      isReady: this.page !== null,
    }
  }

  /**
   * Get the current page
   */
  getPage(): PuppeteerPage | null {
    this.resetSessionTimeout()
    return this.page
  }

  /**
   * Update mouse position
   */
  setMousePosition(x: number, y: number): void {
    this.mousePosition = { x, y }
  }

  /**
   * Get collected console logs and optionally clear
   */
  getConsoleLogs(clear = false): string[] {
    const logs = [...this.consoleLogs]
    if (clear) {
      this.consoleLogs = []
    }
    return logs
  }

  /**
   * Wait for console logs to settle
   */
  async waitForConsoleLogs(): Promise<string[]> {
    // Wait a bit for any pending logs
    await new Promise((resolve) => setTimeout(resolve, this.config.consoleTimeout))
    return this.getConsoleLogs()
  }

  /**
   * Take a screenshot in WebP format
   */
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

  /**
   * Close the browser session
   */
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

  /**
   * Check if session is active
   */
  isActive(): boolean {
    return this.browser !== null && this.page !== null
  }

  /**
   * Reset session timeout
   */
  private resetSessionTimeout(): void {
    this.clearSessionTimeout()

    this.sessionTimeoutId = setTimeout(() => {
      console.log('Browser session timed out, closing...')
      this.close()
    }, this.config.sessionTimeout)
  }

  /**
   * Clear session timeout
   */
  private clearSessionTimeout(): void {
    if (this.sessionTimeoutId) {
      clearTimeout(this.sessionTimeoutId)
      this.sessionTimeoutId = null
    }
  }
}

// ============================================================================
// Session Manager (Singleton)
// ============================================================================

/** Global session store */
const sessions = new Map<string, BrowserSession>()

/**
 * Get or create a browser session
 */
export function getSession(sessionId: string, config?: BrowserSessionConfig): BrowserSession {
  let session = sessions.get(sessionId)

  if (!session) {
    session = new BrowserSession(config)
    sessions.set(sessionId, session)
  }

  return session
}

/**
 * Close and remove a session
 */
export async function closeSession(sessionId: string): Promise<void> {
  const session = sessions.get(sessionId)
  if (session) {
    await session.close()
    sessions.delete(sessionId)
  }
}

/**
 * Close all sessions
 */
export async function closeAllSessions(): Promise<void> {
  const closePromises = Array.from(sessions.values()).map((s) => s.close())
  await Promise.all(closePromises)
  sessions.clear()
}

/**
 * Get all active session IDs
 */
export function getActiveSessions(): string[] {
  return Array.from(sessions.entries())
    .filter(([, session]) => session.isActive())
    .map(([id]) => id)
}
