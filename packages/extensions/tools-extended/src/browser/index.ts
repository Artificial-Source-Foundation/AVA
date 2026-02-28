/**
 * Browser Tool — Puppeteer-based browser automation.
 *
 * Ported from packages/core/src/tools/browser/index.ts.
 * Uses defineTool() + Zod.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import { type BrowserAction, type BrowserActionResult, executeAction } from './actions.js'
import { BrowserSession } from './session.js'

const BrowserSchema = z.object({
  action: z
    .enum(['launch', 'click', 'type', 'scroll_down', 'scroll_up', 'close'])
    .describe('The browser action to perform'),
  url: z.string().optional().describe('URL to navigate to (required for launch action)'),
  coordinate: z
    .string()
    .optional()
    .describe('Click coordinates as "x,y" (required for click action). Example: "450,300"'),
  text: z.string().optional().describe('Text to type (required for type action)'),
})

type BrowserParams = z.infer<typeof BrowserSchema>

function validateParams(params: BrowserParams): { error?: string } {
  switch (params.action) {
    case 'launch':
      if (!params.url) return { error: 'URL is required for launch action' }
      break
    case 'click':
      if (!params.coordinate)
        return { error: 'Coordinate is required for click action (e.g., "450,300")' }
      break
    case 'type':
      if (!params.text) return { error: 'Text is required for type action' }
      break
  }
  return {}
}

function formatResult(
  action: string,
  result: BrowserActionResult
): { success: boolean; output: string; error?: string; metadata?: Record<string, unknown> } {
  if (!result.success) {
    return {
      success: false,
      output: result.error ?? 'Action failed',
      error: 'BROWSER_ACTION_FAILED',
    }
  }

  const parts: string[] = [`Browser action '${action}' completed successfully.`]

  if (result.currentUrl) {
    parts.push(`\n**Current URL**: ${result.currentUrl}`)
  }
  if (result.currentMousePosition) {
    parts.push(`**Mouse Position**: ${result.currentMousePosition}`)
  }
  if (result.logs) {
    parts.push(`\n**Console Logs**:\n\`\`\`\n${result.logs}\n\`\`\``)
  }
  if (result.screenshot) {
    parts.push(`\n**Screenshot**: [Image data available in metadata]`)
  }

  return {
    success: true,
    output: parts.join('\n'),
    metadata: {
      action,
      screenshot: result.screenshot,
      currentUrl: result.currentUrl,
      mousePosition: result.currentMousePosition,
      hasLogs: !!result.logs,
    },
  }
}

export const browserTool = defineTool({
  name: 'browser',
  description: `Launch and interact with a web browser for testing and web automation.

## Actions

### launch
Navigate to a URL and capture the initial state.
\`\`\`json
{"action": "launch", "url": "https://example.com"}
\`\`\`

### click
Click at specific coordinates on the page.
\`\`\`json
{"action": "click", "coordinate": "450,300"}
\`\`\`

### type
Type text at the current cursor position. Click on an input field first.
\`\`\`json
{"action": "type", "text": "Hello World"}
\`\`\`

### scroll_down / scroll_up
Scroll the page by 600 pixels.
\`\`\`json
{"action": "scroll_down"}
\`\`\`

### close
Close the browser session when done.

## Notes
- Screenshots are returned as WebP data URIs
- Console logs are captured during page interaction
- Browser runs in headless mode by default
- Sessions timeout after 5 minutes of inactivity`,

  schema: BrowserSchema,

  permissions: ['execute'],

  execute: async (params: BrowserParams, ctx) => {
    const validation = validateParams(params)
    if (validation.error) {
      return { success: false, output: validation.error, error: 'VALIDATION_ERROR' }
    }

    if (params.action !== 'close' && !(await BrowserSession.isAvailable())) {
      return {
        success: false,
        output: `Puppeteer is not installed. Install it with:\n\nnpm install puppeteer`,
        error: 'PUPPETEER_NOT_INSTALLED',
      }
    }

    const result = await executeAction(ctx.sessionId, params.action as BrowserAction, {
      url: params.url,
      coordinate: params.coordinate,
      text: params.text,
    })

    return formatResult(params.action, result)
  },
})

export type { BrowserAction, BrowserActionResult } from './actions.js'
export type { BrowserSessionConfig, BrowserState } from './session.js'
export { BrowserSession, closeAllSessions, getActiveSessions } from './session.js'
