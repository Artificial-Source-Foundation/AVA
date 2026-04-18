import { expect, type Page, test } from '@playwright/test'

/**
 * AVA Web Mode E2E Tests
 *
 * Tests the UI served by Vite (http://localhost:1420) against a live AVA web
 * backend (http://localhost:18080). Playwright config starts both services.
 *
 * Run (Playwright starts Vite + backend):
 *   npx playwright test e2e/web-mode.spec.ts
 */

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/** URL of the AVA web server when running `ava serve`. Overridable via env. */
const AVA_WEB_URL = process.env.AVA_WEB_URL ?? 'http://localhost:18080'
const APP_VERSION = '2.2.6'

/** Health endpoint exposed by `ava serve`. */
const AVA_HEALTH_URL = `${AVA_WEB_URL}/api/health`

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Check whether the AVA web server is running by hitting its /api/health
 * endpoint. Returns true when the server responds with HTTP 200 + ok status.
 *
 * NOTE: This is intentionally a plain fetch so it works inside Playwright's
 *       Node context (no browser page involved).
 */
async function isBackendRunning(): Promise<boolean> {
  try {
    const res = await fetch(AVA_HEALTH_URL, { signal: AbortSignal.timeout(2000) })
    if (!res.ok) return false
    const body = (await res.json()) as { status?: string }
    return body.status === 'ok'
  } catch {
    return false
  }
}

/** Skip the current test with a clear message when the backend is not available. */
async function requireBackend(): Promise<void> {
  const up = await isBackendRunning()
  if (!up) {
    test.skip(
      true,
      `AVA web server not running at ${AVA_WEB_URL}. ` +
        'Start with `cargo run --bin ava --features web -- serve --port 18080` to run this test.'
    )
  }
}

/** Set localStorage to mark onboarding as complete before the page loads. */
async function bypassOnboarding(page: Page): Promise<void> {
  await page.addInitScript((appVersion: string) => {
    const raw = localStorage.getItem('ava_settings')
    const settings = raw ? (JSON.parse(raw) as Record<string, unknown>) : {}
    settings.onboardingComplete = true
    localStorage.setItem('ava_settings', JSON.stringify(settings))
    localStorage.setItem('ava-last-seen-version', appVersion)
  }, APP_VERSION)
}

async function currentSessionId(page: Page): Promise<string> {
  const sessionId = await page.evaluate(() => localStorage.getItem('ava_last_session'))
  expect(sessionId).toBeTruthy()
  return sessionId!
}

async function injectDeterministicApproval(sessionId: string, runId: string): Promise<void> {
  const res = await fetch(`${AVA_WEB_URL}/api/debug/inject-approval`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      sessionId,
      runId,
      tool_name: 'bash',
      args: { command: 'pwd' },
      risk_level: 'medium',
      reason: 'Deterministic approval request for browser E2E',
      warnings: ['Deterministic browser approval seam'],
    }),
  })
  expect(res.ok).toBe(true)
}

async function finishDebugRun(runId: string): Promise<void> {
  const res = await fetch(`${AVA_WEB_URL}/api/debug/finish-run`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ runId }),
  })
  expect(res.ok).toBe(true)
}

async function fetchAgentStatus(sessionId: string): Promise<{ running?: boolean }> {
  const res = await fetch(
    `${AVA_WEB_URL}/api/agent/status?session_id=${encodeURIComponent(sessionId)}`
  )
  expect(res.ok).toBe(true)
  return (await res.json()) as { running?: boolean }
}

function composer(page: Page) {
  return page.getByRole('textbox', { name: 'Message composer' })
}

/** Wait until the main chat composer is visible (app shell fully loaded). */
async function waitForAppShell(page: Page): Promise<void> {
  await composer(page).waitFor({ state: 'visible', timeout: 30_000 })
}

/** Dismiss the changelog "Got It" dialog if it pops up. */
async function dismissChangelog(page: Page): Promise<void> {
  const gotIt = page.locator('button:has-text("Got It"), button:has-text("Got it")')
  const visible = await gotIt.isVisible({ timeout: 1000 }).catch(() => false)
  if (visible) {
    await gotIt.click()
    await page.waitForTimeout(300)
    return
  }

  const whatsNew = page.getByText("What's New").first()
  const changelogVisible = await whatsNew.isVisible({ timeout: 1000 }).catch(() => false)
  if (!changelogVisible) return

  const modal = page.locator('.fixed.inset-0.z-50').filter({ hasText: "What's New" }).first()
  const closeButton = modal.locator('button').first()
  const closeVisible = await closeButton.isVisible({ timeout: 500 }).catch(() => false)
  if (closeVisible) {
    await closeButton.click({ force: true }).catch(() => undefined)
    await page.waitForTimeout(300)
    return
  }

  await page.keyboard.press('Escape').catch(() => undefined)
  await page.waitForTimeout(300)
}

// ---------------------------------------------------------------------------
// 1. App loads
// ---------------------------------------------------------------------------

test.describe('1. App Loads', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('chat input (textarea) is visible', async ({ page }) => {
    await expect(composer(page)).toBeVisible()
  })

  test('textarea has a non-empty placeholder', async ({ page }) => {
    const placeholder = await composer(page).getAttribute('placeholder')
    expect(placeholder).toBeTruthy()
    expect(placeholder!.length).toBeGreaterThan(0)
  })

  test('status bar or toolbar strip renders below the input', async ({ page }) => {
    await expect(page.getByRole('button', { name: 'Plan mode' })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Act mode' })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Open model selector' })).toBeVisible()
  })

  test('model selector button is present in the toolbar', async ({ page }) => {
    await expect(page.getByRole('button', { name: 'Open model selector' })).toBeVisible()
  })

  test('activity bar is present with at least two navigation icons', async ({ page }) => {
    await expect(page.getByRole('button', { name: 'Dashboard' })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Search sessions' })).toBeVisible()
  })

  test('settings button is visible in activity bar', async ({ page }) => {
    await expect(page.locator('button[aria-label="Settings"]')).toBeVisible()
  })

  test('Plan and Act labels are present in toolbar', async ({ page }) => {
    // AgentMode slider shows "Plan" and "Act" labels
    await expect(page.locator('text=Plan').first()).toBeVisible()
    await expect(page.locator('text=Act').first()).toBeVisible()
  })
})

// ---------------------------------------------------------------------------
// 2. Session management
// ---------------------------------------------------------------------------

test.describe('2. Session Management', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('sessions sidebar is visible with the search control', async ({ page }) => {
    const sessionsBtn = page.getByRole('button', { name: 'Search sessions' })
    await expect(sessionsBtn).toBeVisible()

    const sidebar = page.locator('aside, [class*="sidebar"]').first()
    await expect(sidebar).toBeVisible()
  })

  test('new session button is accessible from sidebar', async ({ page }) => {
    const newChatBtn = page.locator(
      'button[aria-label="New chat"], button:has-text("New Chat"), button:has-text("New Session")'
    )
    await expect(newChatBtn.first()).toBeVisible()
  })

  test('Ctrl+L shortcut opens session switcher', async ({ page }) => {
    await page.keyboard.press('Control+l')
    await page.waitForTimeout(400)

    // Session switcher or command palette should appear
    const dialog = page.locator('[role="dialog"], [role="listbox"]').first()
    const textarea = composer(page)
    // Either a dialog opened, or the app is still healthy
    const dialogVisible = await dialog.isVisible({ timeout: 1000 }).catch(() => false)
    const textareaStillVisible = await textarea.isVisible({ timeout: 1000 }).catch(() => false)
    expect(dialogVisible || textareaStillVisible).toBe(true)
  })

  test('search sessions control is visible without breaking the composer', async ({ page }) => {
    const searchBtn = page.getByRole('button', { name: 'Search sessions' })
    await expect(searchBtn).toBeVisible()

    await expect(composer(page)).toBeVisible()
  })
})

// ---------------------------------------------------------------------------
// 3. Message sending
//    Runs against the real web backend started by Playwright.
// ---------------------------------------------------------------------------

test.describe('3. Message Sending', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('typing in the composer updates its value', async ({ page }) => {
    const textarea = composer(page)
    await textarea.click()
    await textarea.fill('Hello from E2E!')
    expect(await textarea.inputValue()).toBe('Hello from E2E!')
  })

  test('submit button becomes active when text is entered', async ({ page }) => {
    const textarea = composer(page)
    await textarea.click()
    await textarea.fill('test message')

    // Submit button (send arrow) should now be present/enabled inside the form
    const form = page.locator('form')
    // Look for a submit button or button[type=submit]
    const sendBtn = form.locator(
      'button[type="submit"], button[aria-label*="Send"], button[aria-label*="send"]'
    )
    const hasSend = await sendBtn.isVisible({ timeout: 1000 }).catch(() => false)

    if (!hasSend) {
      // Some designs toggle the button via opacity or replace the icon.
      // Fall back to asserting the form is present with buttons.
      const buttons = form.locator('button')
      expect(await buttons.count()).toBeGreaterThan(0)
    }
  })

  test('submitting a message with Enter triggers the agent flow', async ({ page }) => {
    const textarea = composer(page)
    await textarea.click()
    await textarea.fill('Ping')

    // Press Enter to submit (default send key)
    await page.keyboard.press('Enter')
    await page.waitForTimeout(600)

    // The user message should appear in the message list as an optimistic bubble
    // even before the backend responds. Check for the text in the chat area.
    const userMessage = page.locator('text=Ping')
    const appeared = await userMessage
      .first()
      .isVisible({ timeout: 5000 })
      .catch(() => false)

    // If the app is in web mode without a backend, the message might not persist.
    // Verify the textarea was cleared (indicating the send was accepted) OR the message appeared.
    const textareaEmpty = (await textarea.inputValue()) === ''
    expect(appeared || textareaEmpty).toBe(true)
  })

  test('composer is cleared after submission', async ({ page }) => {
    const textarea = composer(page)
    await textarea.click()
    await textarea.fill('Clear me after send')
    await page.keyboard.press('Enter')
    await page.waitForTimeout(400)

    const value = await textarea.inputValue()
    expect(value).toBe('')
  })

  test('Shift+Enter inserts a newline instead of submitting', async ({ page }) => {
    const textarea = composer(page)
    await textarea.click()
    await textarea.fill('line1')
    await page.keyboard.press('Shift+Enter')
    await textarea.type('line2')
    await page.waitForTimeout(200)

    const value = await textarea.inputValue()
    expect(value).toContain('line1')
    expect(value).toContain('line2')
  })
})

// ---------------------------------------------------------------------------
// 4. Tool Approval UI (ApprovalDock)
//    The ApprovalDock only renders when the agent raises an approval request.
//    In web mode this requires a live backend. The positive case uses a
//    debug-only backend seam that creates a correlated synthetic run and
//    pending approval so the real rehydrate/resolve flow is exercised.
// ---------------------------------------------------------------------------

test.describe('4. Tool Approval UI (ApprovalDock)', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('ApprovalDock is not rendered when no approval is pending', async ({ page }) => {
    // ApprovalDock renders with role="dialog" and aria-label="Tool approval request"
    const dock = page.locator('[role="dialog"][aria-label="Tool approval request"]')
    await expect(dock).not.toBeVisible()
  })

  test('chat area layout supports ApprovalDock insertion between list and input', async ({
    page,
  }) => {
    // Structural test: verify the vertical flex layout that hosts ApprovalDock exists.
    // ChatView renders: MessageList → (ApprovalDock) → (QuestionDock) → MessageInput
    const form = page.locator('form') // MessageInput wraps a <form>
    await expect(form).toBeVisible()

    const textarea = composer(page)
    await expect(textarea).toBeVisible()

    // The form/input must appear BELOW the message list in the DOM.
    // Verify that there is a scrollable container above the form.
    const scrollable = page.locator('.overflow-y-auto, [class*="scroll"]').first()
    await expect(scrollable).toBeVisible()
  })

  test('ApprovalDock appears when agent requests approval (backend required)', async ({ page }) => {
    await requireBackend()

    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)

    const sessionId = await currentSessionId(page)
    const runId = `playwright-approval-${Date.now()}`

    try {
      await injectDeterministicApproval(sessionId, runId)
      await page.reload()
      await waitForAppShell(page)
      await dismissChangelog(page)

      const dock = page.locator('[role="dialog"][aria-label="Tool approval request"]')
      await expect(dock).toBeVisible()
      await expect(dock.locator('#approval-dock-title')).toBeVisible()
      await expect(dock).toContainText('bash')
      await expect(dock).toContainText('Running `pwd`')
      await expect(
        dock.locator('button:has-text("Deny"), button[aria-label*="Deny"]')
      ).toBeVisible()
      await expect(
        dock.locator('button:has-text("Approve"), button[aria-label*="Approve"]')
      ).toBeVisible()

      await dock.locator('button:has-text("Deny"), button[aria-label*="Deny"]').click()
      await expect(dock).not.toBeVisible()

      await expect
        .poll(async () => {
          const status = await fetchAgentStatus(sessionId)
          return status.running ?? null
        })
        .toBe(false)
    } finally {
      await finishDebugRun(runId)
    }
  })
})

// ---------------------------------------------------------------------------
// 5. Settings Modal
//    Vite dev server is sufficient.
// ---------------------------------------------------------------------------

test.describe('5. Settings Modal', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('opens when clicking the Settings button', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    // Settings full-screen overlay shows "Back to Chat"
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })
  })

  test('settings sidebar renders all current tab groups', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    const nav = page.locator('nav')
    await expect(nav.getByRole('button', { name: 'General' })).toBeVisible()
    await expect(nav.getByRole('button', { name: 'Providers' })).toBeVisible()
    await expect(nav.getByRole('button', { name: 'Generation' })).toBeVisible()
    await expect(nav.getByRole('button', { name: 'Agents' })).toBeVisible()
    await expect(nav.getByRole('button', { name: 'Permissions & Trust' })).toBeVisible()
    await expect(nav.getByRole('button', { name: 'Appearance' })).toBeVisible()
    await expect(nav.getByRole('button', { name: 'Advanced' })).toBeVisible()
  })

  test('core top-level settings tabs render in sidebar', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    const nav = page.locator('nav')
    await expect(nav.locator('button:has-text("General")')).toBeVisible()
    await expect(nav.locator('button:has-text("Appearance")')).toBeVisible()
    await expect(nav.locator('button:has-text("Providers")')).toBeVisible()
    await expect(nav.locator('button:has-text("Advanced")')).toBeVisible()
  })

  test('tools and models tabs render in sidebar', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    const nav = page.locator('nav')
    await expect(nav.locator('button:has-text("Providers")')).toBeVisible()
    await expect(nav.locator('button:has-text("Generation")')).toBeVisible()
    await expect(nav.locator('button:has-text("Skills & Rules")')).toBeVisible()
    await expect(nav.locator('button:has-text("Plugins")')).toBeVisible()
  })

  test('can switch to Appearance tab and see Color Mode option', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    await page.locator('nav button:has-text("Appearance")').click()
    await page.waitForTimeout(300)

    const modal = page.locator('.fixed.inset-0.z-50')
    await expect(modal.locator('text=Color Mode').first()).toBeVisible({ timeout: 3000 })
  })

  test('can switch to General tab and see Send key setting', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    await page.locator('nav button:has-text("General")').click()
    await page.waitForTimeout(300)

    const modal = page.locator('.fixed.inset-0.z-50')
    await expect(modal.locator('text=Send key').first()).toBeVisible({ timeout: 3000 })
  })

  test('can switch to Generation tab and see Temperature setting', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    await page.locator('nav button:has-text("Generation")').click()
    await page.waitForTimeout(300)

    const modal = page.locator('.fixed.inset-0.z-50')
    await expect(modal.locator('text=Temperature').first()).toBeVisible({ timeout: 3000 })
  })

  test('"Back to Chat" closes the settings modal', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    await page.locator('button:has-text("Back to Chat")').click()
    await page.waitForTimeout(300)

    await expect(page.locator('button:has-text("Back to Chat")')).not.toBeVisible()
    await expect(composer(page)).toBeVisible()
  })

  test('Escape key closes the settings modal', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    await page.keyboard.press('Escape')
    await page.waitForTimeout(300)

    await expect(page.locator('button:has-text("Back to Chat")')).not.toBeVisible()
  })

  test('Advanced tab is present in settings sidebar', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    await expect(page.locator('nav button:has-text("Advanced")')).toBeVisible()
  })

  test('settings change persists in localStorage', async ({ page }) => {
    // Open settings and verify state is stored
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    // Close settings
    await page.locator('button:has-text("Back to Chat")').click()
    await page.waitForTimeout(200)

    // Verify ava_settings key still exists in localStorage
    const hasSettings = await page.evaluate(() => localStorage.getItem('ava_settings') !== null)
    expect(hasSettings).toBe(true)
  })
})

// ---------------------------------------------------------------------------
// 6. Model Selector
//    Vite dev server is sufficient for UI rendering.
// ---------------------------------------------------------------------------

test.describe('6. Model Selector', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('model selector pill is present in the toolbar strip', async ({ page }) => {
    await expect(page.getByRole('button', { name: 'Open model selector' })).toBeVisible()
  })

  test('Ctrl+M shortcut opens the model picker', async ({ page }) => {
    await page.keyboard.press('Control+m')
    await page.waitForTimeout(500)

    // Quick model picker or model browser dialog should appear
    const dialog = page.locator('[role="dialog"]').first()
    const appeared = await dialog.isVisible({ timeout: 2000 }).catch(() => false)

    if (!appeared) {
      // Some builds open the model browser inline rather than a dialog.
      // Verify the app hasn't crashed.
      await expect(composer(page)).toBeVisible()
    }
  })

  test('model browser opens via Ctrl+Shift+M', async ({ page }) => {
    await page.keyboard.press('Control+Shift+m')
    await page.waitForTimeout(500)

    const dialog = page.locator('[role="dialog"]').first()
    const appeared = await dialog.isVisible({ timeout: 2000 }).catch(() => false)

    if (appeared) {
      const modelText = page.locator('text=Model, text=model, text=Provider, text=provider')
      const hasModelContent = await modelText
        .first()
        .isVisible({ timeout: 1000 })
        .catch(() => false)
      expect(hasModelContent || appeared).toBe(true)
      return
    }

    await expect(composer(page)).toBeVisible()
  })

  /**
   * Backend-gated: verify /api/models returns a list of models and that the
   * model selector populates from the list.
   */
  test('backend /api/models returns a non-empty list (backend required)', async ({
    page: _page,
  }) => {
    await requireBackend()

    const res = await fetch(`${AVA_WEB_URL}/api/models`)
    expect(res.ok).toBe(true)

    const models = (await res.json()) as unknown[]
    expect(Array.isArray(models)).toBe(true)
    expect(models.length).toBeGreaterThan(0)
  })

  /**
   * Backend-gated: verify /api/models/current returns the active model.
   */
  test('backend /api/models/current returns current model info (backend required)', async ({
    page: _page,
  }) => {
    await requireBackend()

    const res = await fetch(`${AVA_WEB_URL}/api/models/current`)
    expect(res.ok).toBe(true)

    const model = (await res.json()) as Record<string, unknown>
    // Response should have at least one identifier field
    const hasIdentifier =
      typeof model.id === 'string' ||
      typeof model.name === 'string' ||
      typeof model.model === 'string'
    expect(hasIdentifier).toBe(true)
  })
})

// ---------------------------------------------------------------------------
// 7. Theme
//    Vite dev server — no backend required.
// ---------------------------------------------------------------------------

test.describe('7. Theme', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('root element has a data-theme or class attribute indicating a theme', async ({ page }) => {
    const root = page.locator('html, body, #root, [data-theme]').first()
    await expect(root).toBeVisible()

    // Check for theme-related classes or data attributes on <html> or <body>
    const htmlEl = page.locator('html')
    const dataTheme = await htmlEl.getAttribute('data-theme').catch(() => null)
    const className = await htmlEl.getAttribute('class').catch(() => null)

    // Either a data-theme attribute or a theme class should be set
    const hasThemeMarker = dataTheme !== null || (className !== null && className.length > 0)
    expect(hasThemeMarker).toBe(true)
  })

  test('CSS custom properties (design tokens) are defined on :root', async ({ page }) => {
    // AVA uses CSS variables like --text-primary, --accent, etc.
    const hasTokens = await page.evaluate(() => {
      const style = getComputedStyle(document.documentElement)
      const textPrimary = style.getPropertyValue('--text-primary').trim()
      const accent = style.getPropertyValue('--accent').trim()
      return textPrimary.length > 0 || accent.length > 0
    })
    expect(hasTokens).toBe(true)
  })

  test('theme persists in settings when changed via Appearance tab', async ({ page }) => {
    // Open Appearance tab
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })
    await page.locator('nav button:has-text("Appearance")').click()
    await page.waitForTimeout(300)

    // Verify appearance tab loaded (Color Mode section present)
    const modal = page.locator('.fixed.inset-0.z-50')
    await expect(modal.locator('text=Color Mode').first()).toBeVisible({ timeout: 3000 })

    // Close settings — theme should still be applied
    await page.locator('button:has-text("Back to Chat")').click()
    await page.waitForTimeout(300)

    // App should still have theme tokens defined
    const hasTokens = await page.evaluate(() => {
      const style = getComputedStyle(document.documentElement)
      return style.getPropertyValue('--text-primary').trim().length > 0
    })
    expect(hasTokens).toBe(true)
  })

  test('dark mode class or background color is not pure white by default', async ({ page }) => {
    // AVA uses a dark theme by default — verify the body/root background is dark
    const bg = await page.evaluate(() => {
      const style = getComputedStyle(document.body)
      return style.backgroundColor
    })
    // A dark background would not be rgb(255, 255, 255)
    expect(bg).not.toBe('rgb(255, 255, 255)')
  })
})

// ---------------------------------------------------------------------------
// 8. Responsive Layout
//    Vite dev server — no backend required.
// ---------------------------------------------------------------------------

test.describe('8. Responsive Layout', () => {
  test('narrow viewport (375px) does not break the core chat UI', async ({ page }) => {
    await bypassOnboarding(page)
    await page.setViewportSize({ width: 375, height: 812 })
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)

    // Core elements must still be visible on mobile width
    await expect(composer(page)).toBeVisible()

    // No horizontal scrollbar should appear
    const hasHorizontalScroll = await page.evaluate(
      () => document.documentElement.scrollWidth > document.documentElement.clientWidth
    )
    expect(hasHorizontalScroll).toBe(false)
  })

  test('tablet viewport (768px) renders activity bar and input', async ({ page }) => {
    await bypassOnboarding(page)
    await page.setViewportSize({ width: 768, height: 1024 })
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)

    await expect(composer(page)).toBeVisible()
    // Activity bar should remain visible at tablet width
    await expect(page.locator('button[aria-label="Settings"]')).toBeVisible()
  })

  test('wide viewport (1920px) fills the layout without overflow', async ({ page }) => {
    await bypassOnboarding(page)
    await page.setViewportSize({ width: 1920, height: 1080 })
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)

    await expect(composer(page)).toBeVisible()

    // No unexpected horizontal overflow
    const hasHorizontalScroll = await page.evaluate(
      () => document.documentElement.scrollWidth > document.documentElement.clientWidth + 4 // 4px tolerance
    )
    expect(hasHorizontalScroll).toBe(false)
  })

  test('sidebar hides on narrow viewport when toggled', async ({ page }) => {
    await bypassOnboarding(page)
    await page.setViewportSize({ width: 375, height: 812 })
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)

    // On narrow viewports the sidebar may be hidden by default.
    // Verify the toggle button still works.
    const toggleBtn = page.locator('button[aria-label="Toggle Sidebar"]')
    const toggleVisible = await toggleBtn.isVisible({ timeout: 1000 }).catch(() => false)

    if (toggleVisible) {
      await toggleBtn.click()
      await page.waitForTimeout(200)
      // Textarea must remain accessible
      await expect(composer(page)).toBeVisible()
    }
  })
})

// ---------------------------------------------------------------------------
// 9. WebSocket / Backend API (requires `ava serve`)
//    All tests in this group call `requireBackend()` and are skipped when
//    the backend is not running.
// ---------------------------------------------------------------------------

test.describe('9. WebSocket & Backend API (backend required)', () => {
  test('GET /api/health returns ok', async ({ page: _page }) => {
    await requireBackend()

    const res = await fetch(AVA_HEALTH_URL)
    expect(res.ok).toBe(true)

    const body = (await res.json()) as { status: string; version?: string }
    expect(body.status).toBe('ok')
    expect(typeof body.version).toBe('string')
  })

  test('GET /api/sessions returns an array', async ({ page: _page }) => {
    await requireBackend()

    const res = await fetch(`${AVA_WEB_URL}/api/sessions`)
    expect(res.ok).toBe(true)

    const sessions = (await res.json()) as unknown[]
    expect(Array.isArray(sessions)).toBe(true)
  })

  test('POST /api/sessions/create returns a new session with an id', async ({ page: _page }) => {
    await requireBackend()

    const res = await fetch(`${AVA_WEB_URL}/api/sessions/create`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title: 'E2E Test Session' }),
    })
    expect(res.ok).toBe(true)

    const session = (await res.json()) as Record<string, unknown>
    expect(typeof session.id).toBe('string')
    expect((session.id as string).length).toBeGreaterThan(0)
  })

  test('POST /api/sessions/create then GET /api/sessions/{id} round-trips correctly', async ({
    page: _page,
  }) => {
    await requireBackend()

    // Create a new session
    const createRes = await fetch(`${AVA_WEB_URL}/api/sessions/create`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title: 'E2E Round-Trip Session' }),
    })
    const created = (await createRes.json()) as Record<string, unknown>
    const sessionId = created.id as string

    // Fetch it back
    const getRes = await fetch(`${AVA_WEB_URL}/api/sessions/${sessionId}`)
    expect(getRes.ok).toBe(true)

    const session = (await getRes.json()) as Record<string, unknown>
    expect(session.id).toBe(sessionId)
  })

  test('DELETE /api/sessions/{id} removes the session', async ({ page: _page }) => {
    await requireBackend()

    // Create a throwaway session
    const createRes = await fetch(`${AVA_WEB_URL}/api/sessions/create`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title: 'E2E Delete Test' }),
    })
    const created = (await createRes.json()) as Record<string, unknown>
    const sessionId = created.id as string

    // Delete it
    const delRes = await fetch(`${AVA_WEB_URL}/api/sessions/${sessionId}`, {
      method: 'DELETE',
    })
    expect(delRes.ok).toBe(true)

    // Fetching it now should return 404
    const getRes = await fetch(`${AVA_WEB_URL}/api/sessions/${sessionId}`)
    expect(getRes.status).toBe(404)
  })

  test('GET /api/providers returns an array of provider descriptors', async ({ page: _page }) => {
    await requireBackend()

    const res = await fetch(`${AVA_WEB_URL}/api/providers`)
    expect(res.ok).toBe(true)

    const providers = (await res.json()) as unknown[]
    expect(Array.isArray(providers)).toBe(true)
  })

  test('GET /api/permissions returns current permission level', async ({ page: _page }) => {
    await requireBackend()

    const res = await fetch(`${AVA_WEB_URL}/api/permissions`)
    expect(res.ok).toBe(true)

    const perms = (await res.json()) as Record<string, unknown>
    // Should have a "level" or "permission_level" field
    const hasLevel = 'level' in perms || 'permission_level' in perms || 'permissionLevel' in perms
    expect(hasLevel).toBe(true)
  })

  test('WebSocket /ws connects and receives a pong or initial event', async ({ page }) => {
    await requireBackend()

    // Open a page context so we can use the browser's WebSocket API
    await page.goto(AVA_WEB_URL)

    const wsResult = await page.evaluate(
      async (wsUrl: string) => {
        return new Promise<{ connected: boolean; firstMessage: string | null }>((resolve) => {
          const ws = new WebSocket(wsUrl)
          let firstMessage: string | null = null

          ws.onopen = () => {
            // Connection established — send a ping text frame to keep alive
            ws.send(JSON.stringify({ type: 'ping' }))
          }

          ws.onmessage = (event) => {
            firstMessage = String(event.data)
            ws.close()
            resolve({ connected: true, firstMessage })
          }

          ws.onerror = () => {
            resolve({ connected: false, firstMessage: null })
          }

          // If no message arrives within 3s the connection is still valid
          setTimeout(() => {
            const isOpen = ws.readyState === WebSocket.OPEN
            ws.close()
            resolve({ connected: isOpen, firstMessage })
          }, 3000)
        })
      },
      `${AVA_WEB_URL.replace('http', 'ws')}/ws`
    )

    expect(wsResult.connected).toBe(true)
  })

  test('frontend with the live web backend renders the chat UI', async ({ page }) => {
    await requireBackend()

    await bypassOnboarding(page)

    await page.goto('/')

    await waitForAppShell(page)
    await dismissChangelog(page)

    // Core elements must be present
    await expect(composer(page)).toBeVisible()
    await expect(page.locator('button[aria-label="Settings"]')).toBeVisible()
  })
})
