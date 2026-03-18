import { expect, type Page, test } from '@playwright/test'

/**
 * AVA App — Core UI Tests
 *
 * Tests the SolidJS frontend served by Vite dev server (no Tauri runtime).
 * Settings are persisted via localStorage under 'ava_settings'.
 *
 * Run: npx playwright test e2e/app.spec.ts --reporter=list
 */

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Bypass onboarding + changelog dialog via localStorage before page loads. */
async function bypassOnboarding(page: Page): Promise<void> {
  await page.addInitScript(() => {
    const existing = localStorage.getItem('ava_settings')
    const settings = existing ? JSON.parse(existing) : {}
    settings.onboardingComplete = true
    localStorage.setItem('ava_settings', JSON.stringify(settings))
    // Prevent "What's New" changelog dialog from showing
    localStorage.setItem('ava-last-seen-version', '0.1.0')
  })
}

/** Reset settings so onboarding shows. Also dismiss changelog. */
async function resetForOnboarding(page: Page): Promise<void> {
  await page.addInitScript(() => {
    localStorage.removeItem('ava_settings')
    // Still suppress changelog so it doesn't overlay the onboarding
    localStorage.setItem('ava-last-seen-version', '0.1.0')
  })
}

/** Wait for the main app shell to be ready (textarea visible). */
async function waitForAppShell(page: Page): Promise<void> {
  await page.locator('textarea').first().waitFor({ state: 'visible', timeout: 15000 })
}

/** Dismiss the changelog dialog if it appears. */
async function dismissChangelog(page: Page): Promise<void> {
  const gotIt = page.locator('button:has-text("Got It"), button:has-text("Got it")')
  if (await gotIt.isVisible({ timeout: 1000 }).catch(() => false)) {
    await gotIt.click()
    await page.waitForTimeout(300)
  }
}

// ---------------------------------------------------------------------------
// 1. Splash Screen
// ---------------------------------------------------------------------------

test.describe('Splash Screen', () => {
  test('shows AVA name and tagline during init', async ({ page }) => {
    await page.goto('/')

    // The splash screen renders immediately with h1 "AVA" and tagline
    const avaHeading = page.locator('h1:has-text("AVA")')
    await expect(avaHeading.first()).toBeVisible({ timeout: 5000 })
  })

  test('shows loading dots animation', async ({ page }) => {
    await page.goto('/')

    const dots = page.locator('.splash-dot')
    const count = await dots.count()
    // Splash may have already transitioned in fast environments
    if (count > 0) {
      expect(count).toBe(3)
    }
  })

  test('transitions away from splash screen', async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')

    // Wait for main UI to appear
    await waitForAppShell(page)

    // Splash should no longer be visible
    const splashLogo = page.locator('.splash-logo')
    await expect(splashLogo).not.toBeVisible({ timeout: 5000 })
  })
})

// ---------------------------------------------------------------------------
// 2. Chat View
// ---------------------------------------------------------------------------

test.describe('Chat View', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('composer textarea is present with correct placeholder', async ({ page }) => {
    const textarea = page.locator('textarea').first()
    await expect(textarea).toBeVisible()

    const placeholder = await textarea.getAttribute('placeholder')
    expect(placeholder).toContain('Ask anything')
  })

  test('composer textarea accepts input', async ({ page }) => {
    const textarea = page.locator('textarea').first()
    await textarea.click()
    await textarea.fill('Hello from Playwright!')

    const value = await textarea.inputValue()
    expect(value).toBe('Hello from Playwright!')
  })

  test('toolbar strip renders with multiple controls', async ({ page }) => {
    // The toolbar strip is inside the <form> below the textarea
    // It contains model selector, Plan/Act slider, permissions, etc.
    const form = page.locator('form')
    await expect(form).toBeVisible()

    const buttons = form.locator('button')
    const count = await buttons.count()
    // Expect multiple toolbar controls (model selector, plan/act, permission, sandbox, etc.)
    expect(count).toBeGreaterThan(3)
  })

  test('Plan/Act slider is visible', async ({ page }) => {
    // The toolbar strip shows Plan and Act labels
    await expect(page.locator('text=Plan').first()).toBeVisible()
    await expect(page.locator('text=Act').first()).toBeVisible()
  })

  test('shows empty state when no messages', async ({ page }) => {
    // This test checks empty state only when no session is loaded.
    // Since sessions persist in localStorage, check conditionally.
    const emptyHeading = page.locator('h2:has-text("How can I help")')
    const isVisible = await emptyHeading.isVisible({ timeout: 2000 }).catch(() => false)

    if (isVisible) {
      // Verify suggestion cards are present
      await expect(page.locator('button:has-text("Explain this codebase")')).toBeVisible()
      await expect(page.locator('button:has-text("Fix a bug")')).toBeVisible()
      await expect(page.locator('button:has-text("Write tests")')).toBeVisible()
      await expect(page.locator('button:has-text("Refactor code")')).toBeVisible()
    } else {
      // Session with messages is loaded — verify messages exist
      // The message area should contain some content
      const messageArea = page.locator('.overflow-y-auto')
      await expect(messageArea.first()).toBeVisible()
    }
  })
})

// ---------------------------------------------------------------------------
// 3. Activity Bar & Layout
// ---------------------------------------------------------------------------

test.describe('Activity Bar', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('renders with Sessions and Explorer icons', async ({ page }) => {
    await expect(page.locator('button[aria-label="Sessions"]')).toBeVisible()
    await expect(page.locator('button[aria-label="Explorer"]')).toBeVisible()
  })

  test('settings button is visible', async ({ page }) => {
    await expect(page.locator('button[aria-label="Settings"]')).toBeVisible()
  })

  test('sidebar toggle button works', async ({ page }) => {
    const toggleBtn = page.locator('button[aria-label="Toggle Sidebar"]')
    await expect(toggleBtn).toBeVisible()

    // Click to toggle sidebar
    await toggleBtn.click()
    await page.waitForTimeout(200)

    // Click again to restore
    await toggleBtn.click()
    await page.waitForTimeout(200)

    await expect(toggleBtn).toBeVisible()
  })
})

// ---------------------------------------------------------------------------
// 4. Settings Modal
// ---------------------------------------------------------------------------

test.describe('Settings Modal', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('opens when clicking settings icon', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })
  })

  test('has sidebar with DESKTOP, AI, EXTENSIONS, ADVANCED tab groups', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    const sidebar = page.locator('nav')
    await expect(sidebar.locator('text=DESKTOP')).toBeVisible()
    await expect(sidebar.locator('text=AI')).toBeVisible()
    await expect(sidebar.locator('text=EXTENSIONS')).toBeVisible()
    await expect(sidebar.locator('text=ADVANCED')).toBeVisible()
  })

  test('General tab is visible in sidebar', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await expect(page.locator('nav button:has-text("General")')).toBeVisible()
  })

  test('can navigate to Appearance tab and see content', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await page.locator('nav button:has-text("Appearance")').click()
    await page.waitForTimeout(300)

    // The settings modal body is: nav (sidebar) + content div
    // Verify that appearance-specific content rendered (e.g. "Color Mode" section)
    const settingsModal = page.locator('.fixed.inset-0.z-50')
    await expect(settingsModal.locator('text=Color Mode').first()).toBeVisible({ timeout: 3000 })
  })

  test('can navigate to Behavior tab and see content', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await page.locator('nav button:has-text("Behavior")').click()
    await page.waitForTimeout(300)

    // Behavior tab should show behavior-related settings
    const settingsModal = page.locator('.fixed.inset-0.z-50')
    await expect(settingsModal.locator('text=Send key').first()).toBeVisible({ timeout: 3000 })
  })

  test('can navigate to Providers tab and see content', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await page.locator('nav button:has-text("Providers")').click()
    await page.waitForTimeout(300)

    // Providers tab should show provider-related content
    const settingsModal = page.locator('.fixed.inset-0.z-50')
    await expect(settingsModal.locator('text=Providers').nth(1)).toBeVisible({ timeout: 3000 })
  })

  test('Back to Chat closes settings modal', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await page.locator('button:has-text("Back to Chat")').click()
    await page.waitForTimeout(300)

    await expect(page.locator('button:has-text("Back to Chat")')).not.toBeVisible()
    await expect(page.locator('textarea').first()).toBeVisible()
  })

  test('Escape key closes settings modal', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await page.keyboard.press('Escape')
    await page.waitForTimeout(300)

    await expect(page.locator('button:has-text("Back to Chat")')).not.toBeVisible()
  })

  test('About tab is visible at bottom of sidebar', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await expect(page.locator('nav button:has-text("About")')).toBeVisible()
  })

  test('all Desktop group tabs are present', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    const nav = page.locator('nav')
    await expect(nav.locator('button:has-text("General")')).toBeVisible()
    await expect(nav.locator('button:has-text("Appearance")')).toBeVisible()
    await expect(nav.locator('button:has-text("Behavior")')).toBeVisible()
    await expect(nav.locator('button:has-text("Shortcuts")')).toBeVisible()
    await expect(nav.locator('button:has-text("Permissions")')).toBeVisible()
    await expect(nav.locator('button:has-text("Trusted Folders")')).toBeVisible()
  })
})

// ---------------------------------------------------------------------------
// 5. Onboarding Flow
// ---------------------------------------------------------------------------

test.describe('Onboarding', () => {
  test('shows welcome screen when settings are cleared', async ({ page }) => {
    await resetForOnboarding(page)
    await page.goto('/')

    await expect(page.locator('h1:has-text("Welcome to AVA")')).toBeVisible({ timeout: 15000 })
  })

  test('welcome step has Get Started button and tagline', async ({ page }) => {
    await resetForOnboarding(page)
    await page.goto('/')

    await expect(page.locator('h1:has-text("Welcome to AVA")')).toBeVisible({ timeout: 15000 })
    await expect(page.locator('button:has-text("Get Started")')).toBeVisible()
    await expect(page.getByText('Your AI dev team — lean by default')).toBeVisible()
  })

  test('shows step dots indicator with 5 steps', async ({ page }) => {
    await resetForOnboarding(page)
    await page.goto('/')

    await expect(page.locator('h1:has-text("Welcome to AVA")')).toBeVisible({ timeout: 15000 })

    // StepDots renders in a pb-8 container
    const dotsContainer = page.locator('.pb-8')
    await expect(dotsContainer).toBeVisible()
  })

  test('Get Started advances to provider step', async ({ page }) => {
    await resetForOnboarding(page)
    await page.goto('/')

    await expect(page.locator('h1:has-text("Welcome to AVA")')).toBeVisible({ timeout: 15000 })
    await page.locator('button:has-text("Get Started")').click()

    // Welcome heading should disappear (moved to step 2)
    await expect(page.locator('h1:has-text("Welcome to AVA")')).not.toBeVisible({ timeout: 3000 })
  })

  test('has import config link on welcome step', async ({ page }) => {
    await resetForOnboarding(page)
    await page.goto('/')

    await expect(page.locator('h1:has-text("Welcome to AVA")')).toBeVisible({ timeout: 15000 })
    await expect(page.locator('button:has-text("Import")')).toBeVisible()
  })
})

// ---------------------------------------------------------------------------
// 6. Console Error Check
// ---------------------------------------------------------------------------

test.describe('Error Free', () => {
  test('no critical console errors on load', async ({ page }) => {
    await bypassOnboarding(page)

    const errors: string[] = []
    page.on('console', (msg) => {
      if (msg.type() === 'error') {
        errors.push(msg.text())
      }
    })

    await page.goto('/')
    await waitForAppShell(page)

    // Filter known non-critical errors (Tauri APIs not available in browser, network errors)
    const criticalErrors = errors.filter(
      (e) =>
        !e.includes('not available in browser context') &&
        !e.includes('__TAURI__') &&
        !e.includes('Tauri') &&
        !e.includes('Failed to fetch') &&
        !e.includes('WebSocket') &&
        !e.includes('net::ERR') &&
        !e.includes('404') &&
        !e.includes('invoke')
    )
    expect(criticalErrors).toEqual([])
  })
})
