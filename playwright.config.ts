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

  webServer: [
    {
      command:
        'cargo run --bin ava --features web -- serve --port 18080 --token playwright-local-token',
      port: 18080,
      reuseExistingServer: false,
      timeout: 120_000,
    },
    {
      command:
        'VITE_API_URL=http://localhost:18080 VITE_AVA_SERVER_TOKEN=playwright-local-token VITE_DISABLE_BACKEND_PROXY=1 npx vite --port 11420',
      port: 11420,
      reuseExistingServer: false,
      timeout: 60_000,
    },
  ],
})
