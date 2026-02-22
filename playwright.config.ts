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
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: process.env.CI ? 1 : undefined,
  reporter: 'html',
  timeout: 30_000,

  use: {
    baseURL: 'http://localhost:1420',
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
    command: 'npx vite --port 1420',
    port: 1420,
    reuseExistingServer: !process.env.CI,
    timeout: 60_000,
  },
})
