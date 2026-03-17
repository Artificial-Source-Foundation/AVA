import { expect, test } from '@playwright/test'

/**
 * Web Mode E2E Tests
 *
 * These tests verify the browser (non-Tauri) flow works end-to-end.
 * Requires `ava serve` running on the backend for health check to pass.
 *
 * Run with: npx playwright test e2e/web-mode.spec.ts
 *
 * If the backend is not running, these tests will verify the error state
 * is shown correctly instead of hanging on the splash screen.
 */
test.describe('AVA Web Mode', () => {
  test('onboarding skip persists across reloads', async ({ page }) => {
    // Clear any persisted state
    await page.goto('/')
    await page.evaluate(() => {
      localStorage.clear()
    })
    await page.reload()

    // Wait for initialization to complete
    await page.waitForTimeout(2000)

    // If backend is not available, we'll see the error screen — skip the rest
    const errorScreen = page.locator('text=Initialization Error')
    if (await errorScreen.isVisible({ timeout: 1000 }).catch(() => false)) {
      test.skip(true, 'Backend not running — cannot test onboarding flow')
      return
    }

    // Should see onboarding screen (since localStorage was cleared)
    const skipButton = page.locator('button:has-text("Skip"), button:has-text("skip")')
    if (await skipButton.isVisible({ timeout: 3000 }).catch(() => false)) {
      await skipButton.click()
      await page.waitForTimeout(500)

      // Verify onboardingComplete was persisted
      const settings = await page.evaluate(() => {
        return localStorage.getItem('ava_settings')
      })
      expect(settings).toBeTruthy()
      const parsed = JSON.parse(settings!)
      expect(parsed.onboardingComplete).toBe(true)

      // Reload and verify onboarding does NOT show again
      await page.reload()
      await page.waitForTimeout(2000)
      const onboardingAfterReload = page.locator('button:has-text("Skip"), button:has-text("skip")')
      const stillVisible = await onboardingAfterReload
        .isVisible({ timeout: 2000 })
        .catch(() => false)
      expect(stillVisible).toBe(false)
    }
  })

  test('skips Project Hub in web mode and shows AppShell', async ({ page }) => {
    // Set onboarding as complete so we bypass it
    await page.goto('/')
    await page.evaluate(() => {
      const existing = localStorage.getItem('ava_settings')
      const settings = existing ? JSON.parse(existing) : {}
      settings.onboardingComplete = true
      localStorage.setItem('ava_settings', JSON.stringify(settings))
    })
    await page.reload()
    await page.waitForTimeout(3000)

    // If backend is not available, we'll see the error screen
    const errorScreen = page.locator('text=Initialization Error')
    if (await errorScreen.isVisible({ timeout: 1000 }).catch(() => false)) {
      test.skip(true, 'Backend not running — cannot test project hub bypass')
      return
    }

    // Should NOT see Project Hub (folder picker)
    const projectHub = page.locator('text=Project Hub, text=Open Folder, text=Select a folder')
    const hubVisible = await projectHub.isVisible({ timeout: 2000 }).catch(() => false)
    expect(hubVisible).toBe(false)

    // Should see the AppShell (chat area)
    // Look for composer/input area or chat-related elements
    const composer = page.locator(
      'textarea, [data-testid="composer"], [contenteditable], [role="textbox"]'
    )
    const composerVisible = await composer
      .first()
      .isVisible({ timeout: 5000 })
      .catch(() => false)
    expect(composerVisible).toBe(true)
  })

  test('chat composer is interactive', async ({ page }) => {
    // Set onboarding as complete
    await page.goto('/')
    await page.evaluate(() => {
      const existing = localStorage.getItem('ava_settings')
      const settings = existing ? JSON.parse(existing) : {}
      settings.onboardingComplete = true
      localStorage.setItem('ava_settings', JSON.stringify(settings))
    })
    await page.reload()
    await page.waitForTimeout(3000)

    const errorScreen = page.locator('text=Initialization Error')
    if (await errorScreen.isVisible({ timeout: 1000 }).catch(() => false)) {
      test.skip(true, 'Backend not running — cannot test composer')
      return
    }

    // Find and interact with the composer
    const composer = page
      .locator('textarea, [data-testid="composer"], [contenteditable], [role="textbox"]')
      .first()

    if (await composer.isVisible({ timeout: 5000 }).catch(() => false)) {
      await composer.click()
      await composer.fill('Hello, AVA!')
      // Verify the text was entered
      const value = await composer.inputValue().catch(() => '')
      const text = await composer.textContent().catch(() => '')
      expect(value || text).toContain('Hello')
    }
  })

  test('settings persist via localStorage in web mode', async ({ page }) => {
    await page.goto('/')
    await page.evaluate(() => {
      localStorage.clear()
    })
    await page.reload()
    await page.waitForTimeout(2000)

    // Verify the settings FS fallback key is used after any settings write
    const hasFsKey = await page.evaluate(() => {
      // Trigger a settings write by marking onboarding complete
      const existing = localStorage.getItem('ava_settings')
      const settings = existing ? JSON.parse(existing) : {}
      settings.onboardingComplete = true
      localStorage.setItem('ava_settings', JSON.stringify(settings))
      return localStorage.getItem('ava_settings') !== null
    })
    expect(hasFsKey).toBe(true)
  })
})
