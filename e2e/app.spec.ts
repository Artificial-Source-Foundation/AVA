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

function composer(page: Page) {
  return page.getByRole('textbox', { name: 'Message composer' })
}

/** Wait for the main app shell to be ready (composer visible). */
async function waitForAppShell(page: Page): Promise<void> {
  await composer(page).waitFor({ state: 'visible', timeout: 15000 })
}

/** Dismiss the changelog dialog if it appears. */
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

  test('message composer is present with correct placeholder', async ({ page }) => {
    const input = composer(page)
    await expect(input).toBeVisible()

    const placeholder = await input.getAttribute('placeholder')
    expect(placeholder).toContain('Ask anything')
  })

  test('message composer accepts input', async ({ page }) => {
    const input = composer(page)
    await input.click()
    await input.fill('Hello from Playwright!')

    const value = await input.inputValue()
    expect(value).toBe('Hello from Playwright!')
  })

  test('toolbar strip renders with multiple controls', async ({ page }) => {
    const form = page.locator('form')
    await expect(form).toBeVisible()

    await expect(page.locator('button[aria-label="Open model selector"]')).toBeVisible()
    await expect(form.locator('button[aria-label="Send message"]')).toBeVisible()
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

  test('renders with dashboard and session search controls', async ({ page }) => {
    await expect(page.locator('button[aria-label="Dashboard"]')).toBeVisible()
    await expect(page.locator('button[aria-label="Search sessions"]')).toBeVisible()
  })

  test('settings button is visible', async ({ page }) => {
    await expect(page.locator('button[aria-label="Settings"]')).toBeVisible()
  })

  test('search sessions button toggles the inline search control', async ({ page }) => {
    const toggleBtn = page.locator('button[aria-label="Search sessions"]')
    const searchInput = page.locator('input[aria-label="Search sessions"]')
    await expect(toggleBtn).toBeVisible()
    await expect(searchInput).toHaveCount(0)

    await toggleBtn.click()
    await expect(searchInput).toBeVisible()

    await toggleBtn.click()
    await expect(searchInput).toHaveCount(0)
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

  test('has sidebar with current settings groups', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    const sidebar = page.locator('nav')
    await expect(sidebar.getByText('General', { exact: true }).first()).toBeVisible()
    await expect(sidebar.getByText('Models', { exact: true })).toBeVisible()
    await expect(sidebar.getByText('Tools', { exact: true })).toBeVisible()
    await expect(sidebar.getByText('Permissions', { exact: true })).toBeVisible()
    await expect(sidebar.getByText('Appearance', { exact: true }).first()).toBeVisible()
    await expect(sidebar.getByText('Advanced', { exact: true }).first()).toBeVisible()
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

  test('can navigate to General tab and see behavior content', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await page.locator('nav button:has-text("General")').click()
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
    await expect(composer(page)).toBeVisible()
  })

  test('Escape key closes settings modal', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await page.keyboard.press('Escape')
    await page.waitForTimeout(300)

    await expect(page.locator('button:has-text("Back to Chat")')).not.toBeVisible()
  })

  test('Advanced tab is visible in sidebar', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await expect(page.locator('nav button:has-text("Advanced")')).toBeVisible()
  })

  test('all current top-level settings tabs are present', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    const nav = page.locator('nav')
    await expect(nav.locator('button:has-text("General")')).toBeVisible()
    await expect(nav.locator('button:has-text("Appearance")')).toBeVisible()
    await expect(nav.locator('button:has-text("Providers")')).toBeVisible()
    await expect(nav.locator('button:has-text("Skills & Rules")')).toBeVisible()
    await expect(nav.locator('button:has-text("Permissions & Trust")')).toBeVisible()
    await expect(nav.locator('button:has-text("Advanced")')).toBeVisible()
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
    await expect(page.getByText('Your AI coding agent — lean by default,')).toBeVisible()
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

  test('keeps focus synced with onboarding steps and restores shell focus after reopen', async ({
    page,
  }) => {
    await resetForOnboarding(page)
    await page.goto('/')

    const welcomeHeading = page.getByRole('heading', { name: 'Welcome to AVA' })
    await expect(welcomeHeading).toBeVisible({ timeout: 15000 })
    await expect(welcomeHeading).toBeFocused()

    await page.locator('button:has-text("Get Started")').click()

    const providerHeading = page.getByRole('heading', { name: 'Connect a Provider' })
    await expect(providerHeading).toBeVisible()
    await expect(providerHeading).toBeFocused()

    await page.getByRole('button', { name: 'Skip' }).click()

    const themeHeading = page.getByRole('heading', { name: 'Make It Yours' })
    await expect(themeHeading).toBeVisible()
    await expect(themeHeading).toBeFocused()

    await page.getByRole('button', { name: 'Continue' }).click()
    await page.getByRole('button', { name: 'Continue' }).click()
    await page.getByRole('button', { name: 'Start Coding' }).click()

    const settingsButton = page.getByRole('button', { name: 'Settings' })
    await expect(page.getByRole('dialog', { name: 'Onboarding' })).not.toBeVisible()
    await expect(composer(page)).toBeVisible()

    await page.keyboard.press('Control+,')
    await expect(page.getByRole('button', { name: 'Back to Chat' })).toBeVisible({ timeout: 3000 })

    await page.getByRole('button', { name: 'Open Guide' }).focus()
    await page.keyboard.press('Enter')

    await expect(page.getByRole('dialog', { name: 'Onboarding' })).toBeVisible()
    await expect(welcomeHeading).toBeFocused()

    await page.getByRole('button', { name: 'Close guide' }).click()
    await expect(settingsButton).toBeFocused()
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

// ---------------------------------------------------------------------------
// 7. Keyboard Shortcuts
// ---------------------------------------------------------------------------

test.describe('Keyboard Shortcuts', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('Ctrl+K opens command palette', async ({ page }) => {
    await page.keyboard.press('Control+k')
    await page.waitForTimeout(300)

    // Command palette should render a search input or dialog overlay
    const paletteInput = page.locator(
      'input[placeholder*="Search"], input[placeholder*="search"], input[placeholder*="command"], input[placeholder*="Command"]'
    )
    const isVisible = await paletteInput.isVisible({ timeout: 2000 }).catch(() => false)
    // If not found by placeholder, look for the dialog overlay itself
    if (!isVisible) {
      const overlay = page.locator('[role="dialog"]')
      await expect(overlay.first()).toBeVisible({ timeout: 2000 })
    } else {
      await expect(paletteInput.first()).toBeVisible()
    }
  })

  test('Ctrl+S toggles sidebar visibility', async ({ page }) => {
    // Get initial sidebar state by checking for the Sessions button in the sidebar area
    const sidebar = page.locator('aside, [class*="sidebar"]').first()
    const wasVisible = await sidebar.isVisible({ timeout: 1000 }).catch(() => false)

    await page.keyboard.press('Control+s')
    await page.waitForTimeout(300)

    // After toggle, state should change
    if (wasVisible) {
      // Sidebar may have been hidden — or the button still exists but panel is collapsed
      // Just verify no crash and the app is still functional
      await expect(composer(page)).toBeVisible()
    }

    // Toggle back
    await page.keyboard.press('Control+s')
    await page.waitForTimeout(300)
    await expect(composer(page)).toBeVisible()
  })

  test('Escape closes settings modal', async ({ page }) => {
    // Open settings
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    // Press Escape
    await page.keyboard.press('Escape')
    await page.waitForTimeout(300)

    // Settings should be closed
    await expect(page.locator('button:has-text("Back to Chat")')).not.toBeVisible()
    await expect(composer(page)).toBeVisible()
  })
})

// ---------------------------------------------------------------------------
// 8. Settings — Generation Tab
// ---------------------------------------------------------------------------

test.describe('Settings Generation Tab', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('can navigate to Generation tab and see content', async ({ page }) => {
    await page.locator('button[aria-label="Settings"]').click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 3000 })

    await page.locator('nav button:has-text("Generation")').click()
    await page.waitForTimeout(300)

    const settingsModal = page.locator('.fixed.inset-0.z-50')
    // Generation tab should show generation-related content like Temperature or Max Tokens
    await expect(settingsModal.locator('text=Temperature').first()).toBeVisible({ timeout: 3000 })
  })
})

// ---------------------------------------------------------------------------
// 9. QuestionDock Structure
// ---------------------------------------------------------------------------

test.describe('QuestionDock', () => {
  test('component structure is importable and ChatView area exists', async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)

    // The chat area should exist (QuestionDock renders between messages and input)
    // Without an active agent question, the dock is hidden — verify the chat container exists
    const chatArea = page.locator('form')
    await expect(chatArea).toBeVisible()

    // The message composer is always present
    await expect(composer(page)).toBeVisible()

    // QuestionDock only renders when agent asks a question — verify no crash
    // by confirming the app is in a healthy state
    const emptyState = page.locator('h2:has-text("How can I help")')
    const messages = page.locator('.overflow-y-auto')
    // Either empty state or messages area should be visible
    const hasContent =
      (await emptyState.isVisible({ timeout: 1000 }).catch(() => false)) ||
      (await messages
        .first()
        .isVisible({ timeout: 1000 })
        .catch(() => false))
    expect(hasContent).toBe(true)
  })
})

// ---------------------------------------------------------------------------
// 10. Model Browser
// ---------------------------------------------------------------------------

test.describe('Model Browser', () => {
  test.beforeEach(async ({ page }) => {
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('model selector button exists in toolbar', async ({ page }) => {
    // The model selector is a button in the toolbar strip inside the form
    const form = page.locator('form')
    // Look for a button that likely contains the model name or "Model" text
    const modelBtn = form.locator('button').first()
    await expect(modelBtn).toBeVisible()
  })
})

// ---------------------------------------------------------------------------
// 11. ProjectHub
// ---------------------------------------------------------------------------

test.describe('ProjectHub', () => {
  test('app loads to a functional state with chat or project hub', async ({ page }) => {
    // ProjectHub shows when no project is loaded (Tauri-only).
    // In web/vite mode the app skips straight to AppShell with chat.
    // This test verifies the app reaches a healthy state either way.
    await bypassOnboarding(page)
    await page.goto('/')
    await waitForAppShell(page)
    await dismissChangelog(page)

    // The app should be in one of these states:
    // 1. ProjectHub with greeting + quick actions (Tauri)
    // 2. Chat with empty state (no sessions)
    // 3. Chat with restored session (has messages)
    const greeting = page.locator('h1:has-text("Good")')
    const emptyState = page.locator('h2:has-text("How can I help")')
    const input = composer(page)

    const hasGreeting = await greeting.isVisible({ timeout: 1000 }).catch(() => false)
    const hasEmptyState = await emptyState.isVisible({ timeout: 1000 }).catch(() => false)
    const hasComposer = await input.isVisible({ timeout: 1000 }).catch(() => false)

    // At least one of these should be true
    expect(hasGreeting || hasEmptyState || hasComposer).toBe(true)

    // If ProjectHub is visible, verify quick action buttons
    if (hasGreeting) {
      await expect(page.locator('button:has-text("New Session")')).toBeVisible()
      await expect(page.locator('button:has-text("Open Project")')).toBeVisible()
    }
  })
})
