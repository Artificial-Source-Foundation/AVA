/**
 * Browser Tool
 * Puppeteer-based browser automation for web testing and interaction
 *
 * Actions:
 * - launch: Navigate to a URL and capture screenshot
 * - click: Click at x,y coordinates
 * - type: Type text at current cursor position
 * - scroll_down: Scroll page down 600px
 * - scroll_up: Scroll page up 600px
 * - close: Close the browser session
 *
 * Based on Cline's browser automation pattern
 */

import { z } from 'zod'
import { defineTool } from '../define.js'
import { type BrowserAction, type BrowserActionResult, executeAction } from './actions.js'
import { BrowserSession } from './session.js'

// ============================================================================
// Schema
// ============================================================================

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

// ============================================================================
// Tool Definition
// ============================================================================

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
Click at specific coordinates on the page. Use the screenshot to identify element positions.
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
\`\`\`json
{"action": "close"}
\`\`\`

## Notes
- Screenshots are returned as WebP data URIs
- Console logs are captured during page interaction
- Browser runs in headless mode by default
- Sessions timeout after 5 minutes of inactivity
- Use coordinates from the screenshot to interact with elements`,

  schema: BrowserSchema,

  permissions: ['execute'], // Browser automation is an execution operation

  execute: async (params: BrowserParams, ctx) => {
    // Validate required parameters for each action
    const validation = validateParams(params)
    if (validation.error) {
      return {
        success: false,
        output: validation.error,
        error: 'VALIDATION_ERROR',
      }
    }

    // Check if Puppeteer is available
    if (params.action !== 'close' && !(await BrowserSession.isAvailable())) {
      return {
        success: false,
        output: `Puppeteer is not installed. Install it with:

npm install puppeteer

Or if you prefer puppeteer-core with your own Chrome:

npm install puppeteer-core`,
        error: 'PUPPETEER_NOT_INSTALLED',
      }
    }

    // Execute the action
    const result = await executeAction(ctx.sessionId, params.action as BrowserAction, {
      url: params.url,
      coordinate: params.coordinate,
      text: params.text,
    })

    // Format the response
    return formatResult(params.action, result)
  },
})

// ============================================================================
// Helpers
// ============================================================================

/**
 * Validate action-specific required parameters
 */
function validateParams(params: BrowserParams): { error?: string } {
  switch (params.action) {
    case 'launch':
      if (!params.url) {
        return { error: 'URL is required for launch action' }
      }
      break
    case 'click':
      if (!params.coordinate) {
        return { error: 'Coordinate is required for click action (e.g., "450,300")' }
      }
      break
    case 'type':
      if (!params.text) {
        return { error: 'Text is required for type action' }
      }
      break
  }
  return {}
}

/**
 * Format action result for output
 */
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

  const parts: string[] = []

  // Action confirmation
  parts.push(`Browser action '${action}' completed successfully.`)

  // URL
  if (result.currentUrl) {
    parts.push(`\n**Current URL**: ${result.currentUrl}`)
  }

  // Mouse position
  if (result.currentMousePosition) {
    parts.push(`**Mouse Position**: ${result.currentMousePosition}`)
  }

  // Console logs
  if (result.logs) {
    parts.push(`\n**Console Logs**:\n\`\`\`\n${result.logs}\n\`\`\``)
  }

  // Screenshot
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

// ============================================================================
// Re-exports
// ============================================================================

export type { BrowserAction, BrowserActionResult } from './actions.js'
export type { BrowserSessionConfig, BrowserState } from './session.js'
export { BrowserSession, closeAllSessions, getActiveSessions } from './session.js'
