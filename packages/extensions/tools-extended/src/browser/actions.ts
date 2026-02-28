/**
 * Browser Actions — individual browser action handlers.
 *
 * Copied from packages/core/src/tools/browser/actions.ts.
 * Only imports from ./session.js.
 */

/* eslint-disable @typescript-eslint/no-unused-vars */
declare const window: {
  scrollBy(x: number, y: number): void
}

/* eslint-enable @typescript-eslint/no-unused-vars */

import { type BrowserSession, getSession } from './session.js'

export type BrowserAction = 'launch' | 'click' | 'type' | 'scroll_down' | 'scroll_up' | 'close'

export interface BrowserActionResult {
  success: boolean
  screenshot?: string
  logs?: string
  currentUrl?: string
  currentMousePosition?: string
  error?: string
}

export interface ActionContext {
  sessionId: string
  session: BrowserSession
}

const SCROLL_AMOUNT = 600

export async function actionLaunch(ctx: ActionContext, url: string): Promise<BrowserActionResult> {
  try {
    if (!url) {
      return { success: false, error: 'URL is required for launch action' }
    }

    let normalizedUrl = url
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      normalizedUrl = `https://${url}`
    }

    await ctx.session.launch(normalizedUrl)
    await new Promise((resolve) => setTimeout(resolve, 1000))

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

export async function actionClick(
  ctx: ActionContext,
  coordinate: string
): Promise<BrowserActionResult> {
  try {
    const page = ctx.session.getPage()
    if (!page) {
      return { success: false, error: 'No active browser session. Call launch first.' }
    }

    const parts = coordinate.split(',').map((s) => s.trim())
    if (parts.length !== 2) {
      return { success: false, error: 'Invalid coordinate format. Use "x,y" (e.g., "450,300")' }
    }

    const x = parseInt(parts[0] ?? '', 10)
    const y = parseInt(parts[1] ?? '', 10)

    if (Number.isNaN(x) || Number.isNaN(y)) {
      return { success: false, error: 'Invalid coordinates. Must be numbers.' }
    }

    await page.mouse.move(x, y)
    await page.mouse.click(x, y)
    ctx.session.setMousePosition(x, y)

    await new Promise((resolve) => setTimeout(resolve, 500))

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

export async function actionType(ctx: ActionContext, text: string): Promise<BrowserActionResult> {
  try {
    const page = ctx.session.getPage()
    if (!page) {
      return { success: false, error: 'No active browser session. Call launch first.' }
    }

    if (!text) {
      return { success: false, error: 'Text is required for type action' }
    }

    await page.keyboard.type(text, { delay: 50 })
    await new Promise((resolve) => setTimeout(resolve, 300))

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

export async function actionScrollDown(ctx: ActionContext): Promise<BrowserActionResult> {
  try {
    const page = ctx.session.getPage()
    if (!page) {
      return { success: false, error: 'No active browser session. Call launch first.' }
    }

    await page.evaluate((amount: number) => {
      window.scrollBy(0, amount)
    }, SCROLL_AMOUNT)

    await new Promise((resolve) => setTimeout(resolve, 300))

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

export async function actionScrollUp(ctx: ActionContext): Promise<BrowserActionResult> {
  try {
    const page = ctx.session.getPage()
    if (!page) {
      return { success: false, error: 'No active browser session. Call launch first.' }
    }

    await page.evaluate((amount: number) => {
      window.scrollBy(0, -amount)
    }, SCROLL_AMOUNT)

    await new Promise((resolve) => setTimeout(resolve, 300))

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

export async function actionClose(ctx: ActionContext): Promise<BrowserActionResult> {
  try {
    await ctx.session.close()
    return { success: true }
  } catch (err) {
    return {
      success: false,
      error: `Close failed: ${err instanceof Error ? err.message : String(err)}`,
    }
  }
}

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
