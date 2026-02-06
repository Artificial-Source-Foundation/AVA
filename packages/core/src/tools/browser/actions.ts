/**
 * Browser Actions
 * Individual browser actions for the browser tool
 *
 * Based on Cline's browser automation pattern:
 * - launch: Navigate to a URL
 * - click: Click at coordinates
 * - type: Type text at current location
 * - scroll_down: Scroll down 600px
 * - scroll_up: Scroll up 600px
 * - close: Close the browser
 */

/* eslint-disable @typescript-eslint/no-unused-vars */
// Declare browser globals for page.evaluate() context
declare const window: {
  scrollBy(x: number, y: number): void
}

/* eslint-enable @typescript-eslint/no-unused-vars */

import { type BrowserSession, getSession } from './session.js'

// ============================================================================
// Types
// ============================================================================

export type BrowserAction = 'launch' | 'click' | 'type' | 'scroll_down' | 'scroll_up' | 'close'

export interface BrowserActionResult {
  /** Whether the action succeeded */
  success: boolean
  /** Screenshot as data URI (WebP base64) */
  screenshot?: string
  /** Console logs collected */
  logs?: string
  /** Current page URL */
  currentUrl?: string
  /** Current mouse position as "x,y" */
  currentMousePosition?: string
  /** Error message if failed */
  error?: string
}

export interface ActionContext {
  sessionId: string
  session: BrowserSession
}

// ============================================================================
// Constants
// ============================================================================

const SCROLL_AMOUNT = 600 // pixels

// ============================================================================
// Action Handlers
// ============================================================================

/**
 * Launch browser and navigate to URL
 */
export async function actionLaunch(ctx: ActionContext, url: string): Promise<BrowserActionResult> {
  try {
    // Validate URL
    if (!url) {
      return { success: false, error: 'URL is required for launch action' }
    }

    // Ensure URL has protocol
    let normalizedUrl = url
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      normalizedUrl = `https://${url}`
    }

    // Launch and navigate
    await ctx.session.launch(normalizedUrl)

    // Wait for page to settle
    await new Promise((resolve) => setTimeout(resolve, 1000))

    // Capture state
    const screenshot = await ctx.session.takeScreenshot()
    const logs = await ctx.session.waitForConsoleLogs()
    const state = ctx.session.getState()

    return {
      success: true,
      screenshot,
      logs: logs.length > 0 ? logs.join('\n') : undefined,
      currentUrl: state.url,
      currentMousePosition: `${state.mousePosition.x},${state.mousePosition.y}`,
    }
  } catch (err) {
    return {
      success: false,
      error: `Launch failed: ${err instanceof Error ? err.message : String(err)}`,
    }
  }
}

/**
 * Click at specified coordinates
 */
export async function actionClick(
  ctx: ActionContext,
  coordinate: string
): Promise<BrowserActionResult> {
  try {
    const page = ctx.session.getPage()
    if (!page) {
      return { success: false, error: 'No active browser session. Call launch first.' }
    }

    // Parse coordinates
    const parts = coordinate.split(',').map((s) => s.trim())
    if (parts.length !== 2) {
      return { success: false, error: 'Invalid coordinate format. Use "x,y" (e.g., "450,300")' }
    }

    const x = parseInt(parts[0], 10)
    const y = parseInt(parts[1], 10)

    if (Number.isNaN(x) || Number.isNaN(y)) {
      return { success: false, error: 'Invalid coordinates. Must be numbers.' }
    }

    // Move mouse and click
    await page.mouse.move(x, y)
    await page.mouse.click(x, y)

    // Update mouse position
    ctx.session.setMousePosition(x, y)

    // Wait for any navigation or state changes
    await new Promise((resolve) => setTimeout(resolve, 500))

    // Capture state
    const screenshot = await ctx.session.takeScreenshot()
    const logs = await ctx.session.waitForConsoleLogs()
    const state = ctx.session.getState()

    return {
      success: true,
      screenshot,
      logs: logs.length > 0 ? logs.join('\n') : undefined,
      currentUrl: state.url,
      currentMousePosition: `${x},${y}`,
    }
  } catch (err) {
    return {
      success: false,
      error: `Click failed: ${err instanceof Error ? err.message : String(err)}`,
    }
  }
}

/**
 * Type text at current cursor location
 */
export async function actionType(ctx: ActionContext, text: string): Promise<BrowserActionResult> {
  try {
    const page = ctx.session.getPage()
    if (!page) {
      return { success: false, error: 'No active browser session. Call launch first.' }
    }

    if (!text) {
      return { success: false, error: 'Text is required for type action' }
    }

    // Type with small delay to simulate human typing
    await page.keyboard.type(text, { delay: 50 })

    // Wait for any reactions
    await new Promise((resolve) => setTimeout(resolve, 300))

    // Capture state
    const screenshot = await ctx.session.takeScreenshot()
    const logs = await ctx.session.waitForConsoleLogs()
    const state = ctx.session.getState()

    return {
      success: true,
      screenshot,
      logs: logs.length > 0 ? logs.join('\n') : undefined,
      currentUrl: state.url,
      currentMousePosition: `${state.mousePosition.x},${state.mousePosition.y}`,
    }
  } catch (err) {
    return {
      success: false,
      error: `Type failed: ${err instanceof Error ? err.message : String(err)}`,
    }
  }
}

/**
 * Scroll down the page
 */
export async function actionScrollDown(ctx: ActionContext): Promise<BrowserActionResult> {
  try {
    const page = ctx.session.getPage()
    if (!page) {
      return { success: false, error: 'No active browser session. Call launch first.' }
    }

    // Scroll down
    await page.evaluate((amount: number) => {
      window.scrollBy(0, amount)
    }, SCROLL_AMOUNT)

    // Wait for scroll to complete
    await new Promise((resolve) => setTimeout(resolve, 300))

    // Capture state
    const screenshot = await ctx.session.takeScreenshot()
    const state = ctx.session.getState()

    return {
      success: true,
      screenshot,
      currentUrl: state.url,
      currentMousePosition: `${state.mousePosition.x},${state.mousePosition.y}`,
    }
  } catch (err) {
    return {
      success: false,
      error: `Scroll down failed: ${err instanceof Error ? err.message : String(err)}`,
    }
  }
}

/**
 * Scroll up the page
 */
export async function actionScrollUp(ctx: ActionContext): Promise<BrowserActionResult> {
  try {
    const page = ctx.session.getPage()
    if (!page) {
      return { success: false, error: 'No active browser session. Call launch first.' }
    }

    // Scroll up
    await page.evaluate((amount: number) => {
      window.scrollBy(0, -amount)
    }, SCROLL_AMOUNT)

    // Wait for scroll to complete
    await new Promise((resolve) => setTimeout(resolve, 300))

    // Capture state
    const screenshot = await ctx.session.takeScreenshot()
    const state = ctx.session.getState()

    return {
      success: true,
      screenshot,
      currentUrl: state.url,
      currentMousePosition: `${state.mousePosition.x},${state.mousePosition.y}`,
    }
  } catch (err) {
    return {
      success: false,
      error: `Scroll up failed: ${err instanceof Error ? err.message : String(err)}`,
    }
  }
}

/**
 * Close the browser session
 */
export async function actionClose(ctx: ActionContext): Promise<BrowserActionResult> {
  try {
    await ctx.session.close()

    return {
      success: true,
    }
  } catch (err) {
    return {
      success: false,
      error: `Close failed: ${err instanceof Error ? err.message : String(err)}`,
    }
  }
}

// ============================================================================
// Action Dispatcher
// ============================================================================

/**
 * Execute a browser action
 */
export async function executeAction(
  sessionId: string,
  action: BrowserAction,
  params: { url?: string; coordinate?: string; text?: string }
): Promise<BrowserActionResult> {
  const session = getSession(sessionId)
  const ctx: ActionContext = { sessionId, session }

  switch (action) {
    case 'launch':
      return actionLaunch(ctx, params.url ?? '')

    case 'click':
      return actionClick(ctx, params.coordinate ?? '')

    case 'type':
      return actionType(ctx, params.text ?? '')

    case 'scroll_down':
      return actionScrollDown(ctx)

    case 'scroll_up':
      return actionScrollUp(ctx)

    case 'close':
      return actionClose(ctx)

    default:
      return { success: false, error: `Unknown action: ${action}` }
  }
}
