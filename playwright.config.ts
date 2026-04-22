import { defineConfig, devices } from '@playwright/test'

/**
 * Playwright E2E config for AVA's SolidJS frontend.
 *
 * Tests the UI served by Vite's dev server (no Tauri runtime).
 * Tauri-specific APIs are stubbed via Vite's alias config.
 *
 * Run: npx playwright test
 * UI:  npx playwright test --ui
 */
export default defineConfig({
  testDir: './e2e',
  fullyParallel: false,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: 1,
  reporter: 'html',
  timeout: 30_000,

  use: {
    baseURL: 'http://localhost:11420',
    trace: 'on-first-retry',
    screenshot: 'only-on-failure',
  },

  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],

  webServer: {
    command: './scripts/testing/playwright-stack.sh',
    port: 11420,
    reuseExistingServer: false,
    timeout: 300_000,
  },
})
