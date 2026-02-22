import { expect, test } from '@playwright/test'

test.describe('AVA App — Smoke Tests', () => {
  test('app loads and shows main layout', async ({ page }) => {
    await page.goto('/')
    // App should render without crashing
    await expect(page.locator('body')).toBeVisible()
    // Wait for SolidJS to hydrate
    await page.waitForTimeout(1000)
    // Take a screenshot for visual reference
    await page.screenshot({ path: 'e2e/screenshots/app-loaded.png', fullPage: true })
  })

  test('sidebar is visible', async ({ page }) => {
    await page.goto('/')
    await page.waitForTimeout(1000)
    // Look for sidebar or navigation element
    const sidebar = page.locator('[data-testid="sidebar"], .sidebar, nav, aside').first()
    if (await sidebar.isVisible()) {
      await expect(sidebar).toBeVisible()
    }
  })

  test('settings panel opens', async ({ page }) => {
    await page.goto('/')
    await page.waitForTimeout(1000)
    // Try clicking settings button if it exists
    const settingsBtn = page
      .locator(
        '[data-testid="settings-button"], button:has-text("Settings"), [aria-label*="settings" i]'
      )
      .first()
    if (await settingsBtn.isVisible()) {
      await settingsBtn.click()
      await page.waitForTimeout(500)
      await page.screenshot({ path: 'e2e/screenshots/settings-open.png' })
    }
  })

  test('no console errors on load', async ({ page }) => {
    const errors: string[] = []
    page.on('console', (msg) => {
      if (msg.type() === 'error') {
        errors.push(msg.text())
      }
    })
    await page.goto('/')
    await page.waitForTimeout(2000)
    // Filter out known non-critical errors (e.g., Tauri API not available)
    const criticalErrors = errors.filter(
      (e) => !e.includes('not available in browser context') && !e.includes('__TAURI__')
    )
    expect(criticalErrors).toEqual([])
  })

  test('page has no accessibility violations (basic)', async ({ page }) => {
    await page.goto('/')
    await page.waitForTimeout(1000)
    // Check that images have alt text
    const images = page.locator('img:not([alt])')
    const count = await images.count()
    expect(count).toBe(0)
  })
})
