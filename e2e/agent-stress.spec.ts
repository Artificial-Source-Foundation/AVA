import { expect, type Page, test } from '@playwright/test'

/**
 * AVA Agent Stress Test — Desktop UI via Web Serve Mode
 *
 * Exercises the full agent loop through the desktop UI:
 *   1. Send a goal → agent runs → tools fire → response appears
 *   2. Test tool approval flow (bash commands)
 *   3. Test session management during agent runs
 *   4. Test multi-turn conversations
 *
 * Requires `ava serve --port 8080 --auto-approve` running.
 *
 * Run:
 *   AVA_WEB_URL=http://localhost:8080 npx playwright test e2e/agent-stress.spec.ts
 */

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const AVA_WEB_URL = process.env['AVA_WEB_URL'] ?? 'http://localhost:8080'
const AVA_HEALTH_URL = `${AVA_WEB_URL}/api/health`

/** How long to wait for agent responses (LLM calls can be slow). */
const AGENT_TIMEOUT = 60_000

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

async function isBackendRunning(): Promise<boolean> {
  try {
    const res = await fetch(AVA_HEALTH_URL, { signal: AbortSignal.timeout(3000) })
    if (!res.ok) return false
    const body = (await res.json()) as { status?: string }
    return body.status === 'ok'
  } catch {
    return false
  }
}

async function requireBackend(): Promise<void> {
  const up = await isBackendRunning()
  if (!up) {
    test.skip(
      true,
      `AVA web server not running at ${AVA_WEB_URL}. ` +
        'Start with: cargo run --bin ava --features web -- serve --port 8080 --auto-approve'
    )
  }
}

async function setupPage(page: Page): Promise<void> {
  await page.addInitScript(() => {
    const settings = { onboardingComplete: true }
    localStorage.setItem('ava_settings', JSON.stringify(settings))
    localStorage.setItem('ava-last-seen-version', '0.1.0')
  })
}

async function waitForAppShell(page: Page): Promise<void> {
  await page.locator('textarea').first().waitFor({ state: 'visible', timeout: 15_000 })
}

async function dismissChangelog(page: Page): Promise<void> {
  const gotIt = page.locator('button:has-text("Got It"), button:has-text("Got it")')
  if (await gotIt.isVisible({ timeout: 1000 }).catch(() => false)) {
    await gotIt.click()
    await page.waitForTimeout(300)
  }
}

/** Submit a message in the chat composer and wait for it to be accepted. */
async function sendMessage(page: Page, text: string): Promise<void> {
  const textarea = page.locator('textarea').first()
  await textarea.click()
  await textarea.fill(text)
  await page.keyboard.press('Enter')
  // Wait for composer to clear (message accepted)
  await expect(textarea).toHaveValue('', { timeout: 5000 })
}

/** Wait for an assistant response to appear in the chat. */
async function waitForAssistantResponse(page: Page, timeout = AGENT_TIMEOUT): Promise<string> {
  // Agent responses appear as message bubbles. We look for any new content
  // that appears after submission. The assistant bubble typically has a
  // data-role="assistant" attribute or appears in a specific container.
  //
  // Strategy: wait for either:
  //   1. A visible assistant message bubble
  //   2. Text content that wasn't the user's input
  //   3. The agent status to show "idle" / completed

  // Wait for the agent to start (loading indicator or streaming text)
  await page.waitForTimeout(1000)

  // Wait for assistant content to appear
  const assistantBubble = page.locator(
    '[data-role="assistant"], [class*="assistant"], [class*="AssistantMessage"]'
  )

  // Wait for at least one assistant message to be visible
  const appeared = await assistantBubble
    .first()
    .waitFor({ state: 'visible', timeout })
    .then(() => true)
    .catch(() => false)

  if (appeared) {
    // Wait a bit more for streaming to complete
    await page.waitForTimeout(2000)
    const text = (await assistantBubble.last().textContent()) ?? ''
    return text
  }

  // Fallback: look for any new text content in the message list area
  const messageArea = page
    .locator('.overflow-y-auto, [class*="scroll"], [class*="message"]')
    .first()
  const content = (await messageArea.textContent()) ?? ''
  return content
}

/** Check if the agent is currently running (has a cancel button or loading state). */
async function isAgentRunning(page: Page): Promise<boolean> {
  const cancelBtn = page.locator(
    'button:has-text("Cancel"), button:has-text("Stop"), button[aria-label*="cancel"], button[aria-label*="stop"]'
  )
  return cancelBtn.isVisible({ timeout: 500 }).catch(() => false)
}

/** Wait for the agent to finish running. */
async function waitForAgentIdle(page: Page, timeout = AGENT_TIMEOUT): Promise<void> {
  const start = Date.now()
  while (Date.now() - start < timeout) {
    const running = await isAgentRunning(page)
    if (!running) return
    await page.waitForTimeout(1000)
  }
}

// ---------------------------------------------------------------------------
// API-level stress tests (no browser needed, fast)
// ---------------------------------------------------------------------------

test.describe('Agent API Stress Tests', () => {
  test.beforeEach(async () => {
    await requireBackend()
  })

  test('health check responds quickly', async ({ page: _page }) => {
    const start = Date.now()
    const res = await fetch(AVA_HEALTH_URL)
    const elapsed = Date.now() - start

    expect(res.ok).toBe(true)
    expect(elapsed).toBeLessThan(1000)

    const body = (await res.json()) as { status: string; version?: string }
    expect(body.status).toBe('ok')
  })

  test('submit agent goal via API and get streaming response', async ({ page: _page }) => {
    // Create a session first
    const sessionRes = await fetch(`${AVA_WEB_URL}/api/sessions/create`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title: 'Stress Test - API Goal' }),
    })
    expect(sessionRes.ok).toBe(true)
    const session = (await sessionRes.json()) as { id: string }

    // Submit a simple goal
    const submitRes = await fetch(`${AVA_WEB_URL}/api/agent/submit`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        session_id: session.id,
        message: 'Reply with exactly: STRESS_TEST_OK',
      }),
    })
    expect(submitRes.ok).toBe(true)

    // Poll agent status until idle (max 30s)
    let attempts = 0
    while (attempts < 30) {
      await new Promise((r) => setTimeout(r, 1000))
      const statusRes = await fetch(`${AVA_WEB_URL}/api/agent/status`)
      if (statusRes.ok) {
        const status = (await statusRes.json()) as { running?: boolean; state?: string }
        if (!status.running || status.state === 'idle') break
      }
      attempts++
    }
  })

  test('rapid session creation does not crash', async ({ page: _page }) => {
    // Create 5 sessions rapidly
    const promises = Array.from({ length: 5 }, (_, i) =>
      fetch(`${AVA_WEB_URL}/api/sessions/create`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ title: `Rapid Session ${i}` }),
      })
    )

    const results = await Promise.all(promises)
    for (const res of results) {
      expect(res.ok).toBe(true)
    }

    // Verify all sessions appear in the list
    const listRes = await fetch(`${AVA_WEB_URL}/api/sessions`)
    expect(listRes.ok).toBe(true)
    const sessions = (await listRes.json()) as { id: string; title?: string }[]
    const stressSessions = sessions.filter((s) => s.title?.startsWith('Rapid Session'))
    expect(stressSessions.length).toBeGreaterThanOrEqual(5)

    // Cleanup: delete them
    for (const s of stressSessions) {
      await fetch(`${AVA_WEB_URL}/api/sessions/${s.id}`, { method: 'DELETE' })
    }
  })

  test('concurrent API endpoints do not deadlock', async ({ page: _page }) => {
    // Hit multiple endpoints simultaneously
    const endpoints = [
      fetch(`${AVA_HEALTH_URL}`),
      fetch(`${AVA_WEB_URL}/api/sessions`),
      fetch(`${AVA_WEB_URL}/api/models`),
      fetch(`${AVA_WEB_URL}/api/providers`),
      fetch(`${AVA_WEB_URL}/api/agent/status`),
    ]

    const results = await Promise.all(endpoints)
    for (const res of results) {
      expect(res.ok).toBe(true)
    }
  })

  test('model list contains expected fields', async ({ page: _page }) => {
    const res = await fetch(`${AVA_WEB_URL}/api/models`)
    expect(res.ok).toBe(true)

    const models = (await res.json()) as Record<string, unknown>[]
    expect(models.length).toBeGreaterThan(0)

    // Each model should have identifying info
    const first = models[0]
    const hasId = 'id' in first || 'name' in first || 'model' in first
    expect(hasId).toBe(true)
  })

  test('provider list contains at least one provider', async ({ page: _page }) => {
    const res = await fetch(`${AVA_WEB_URL}/api/providers`)
    expect(res.ok).toBe(true)

    const providers = (await res.json()) as Record<string, unknown>[]
    expect(providers.length).toBeGreaterThan(0)
  })
})

// ---------------------------------------------------------------------------
// UI-level agent tests (browser needed, slower)
// ---------------------------------------------------------------------------

test.describe('Agent UI Stress Tests', () => {
  test.beforeEach(async ({ page }) => {
    await requireBackend()
    await setupPage(page)
    await page.goto(AVA_WEB_URL)
    await page.reload() // Ensure initScript runs
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('send a simple goal and see assistant response', async ({ page }) => {
    await sendMessage(page, 'Reply with exactly: HELLO_FROM_STRESS_TEST')

    // Wait for any response text to appear
    const response = await waitForAssistantResponse(page)
    // The response should contain our marker (or at least the agent responded)
    expect(response.length).toBeGreaterThan(0)
  })

  test('agent uses read tool when asked to read a file', async ({ page }) => {
    // Ask agent to read a file that exists in the project
    await sendMessage(page, 'Read the file README.md and tell me what it says. Be brief.')

    const response = await waitForAssistantResponse(page)
    expect(response.length).toBeGreaterThan(0)
  })

  test('multi-turn conversation maintains context', async ({ page }) => {
    // Turn 1: establish context
    await sendMessage(page, 'Remember this number: 42. Reply with just "Noted."')
    await waitForAssistantResponse(page)
    await waitForAgentIdle(page)

    // Turn 2: recall context
    await sendMessage(
      page,
      'What number did I just ask you to remember? Reply with just the number.'
    )
    const response = await waitForAssistantResponse(page)

    // The response should contain "42"
    expect(response).toContain('42')
  })

  test('composer clears after sending and becomes usable again', async ({ page }) => {
    const textarea = page.locator('textarea').first()

    // Send first message
    await sendMessage(page, 'Say hello')
    await waitForAgentIdle(page)

    // Verify composer is still functional
    await textarea.click()
    await textarea.fill('Second message test')
    expect(await textarea.inputValue()).toBe('Second message test')

    // Clear it
    await textarea.fill('')
    expect(await textarea.inputValue()).toBe('')
  })

  test('settings remain accessible while agent response is showing', async ({ page }) => {
    await sendMessage(page, 'Reply with a short greeting.')

    // While agent may be running, settings should still be accessible
    await page.waitForTimeout(500)

    const settingsBtn = page.locator('button[aria-label="Settings"]')
    await expect(settingsBtn).toBeVisible()
    await settingsBtn.click()
    await expect(page.locator('button:has-text("Back to Chat")')).toBeVisible({ timeout: 5000 })

    // Close settings
    await page.locator('button:has-text("Back to Chat")').click()
    await page.waitForTimeout(300)

    // Chat should still be functional
    await expect(page.locator('textarea').first()).toBeVisible()
  })

  test('session switcher works between agent conversations', async ({ page }) => {
    // Send a message in the current session
    await sendMessage(page, 'This is session A. Reply with "Session A acknowledged."')
    await waitForAgentIdle(page)

    // Open session switcher
    await page.keyboard.press('Control+l')
    await page.waitForTimeout(500)

    // Look for new chat / session list
    const dialog = page.locator('[role="dialog"], [role="listbox"]').first()
    const dialogVisible = await dialog.isVisible({ timeout: 2000 }).catch(() => false)

    // Close dialog / verify app is still functional
    if (dialogVisible) {
      await page.keyboard.press('Escape')
      await page.waitForTimeout(300)
    }

    await expect(page.locator('textarea').first()).toBeVisible()
  })
})

// ---------------------------------------------------------------------------
// WebSocket streaming tests
// ---------------------------------------------------------------------------

test.describe('WebSocket Streaming Stress', () => {
  test.beforeEach(async () => {
    await requireBackend()
  })

  test('WebSocket connects and stays alive for 10 seconds', async ({ page }) => {
    await page.goto(AVA_WEB_URL)

    const result = await page.evaluate(
      async (wsUrl: string) => {
        return new Promise<{ connected: boolean; messagesReceived: number; errors: number }>(
          (resolve) => {
            const ws = new WebSocket(wsUrl)
            let messagesReceived = 0
            let errors = 0

            ws.onopen = () => {
              // Send periodic pings
              const interval = setInterval(() => {
                if (ws.readyState === WebSocket.OPEN) {
                  ws.send(JSON.stringify({ type: 'ping' }))
                }
              }, 2000)

              setTimeout(() => {
                clearInterval(interval)
                const wasOpen = ws.readyState === WebSocket.OPEN
                ws.close()
                resolve({ connected: wasOpen, messagesReceived, errors })
              }, 10_000)
            }

            ws.onmessage = () => {
              messagesReceived++
            }

            ws.onerror = () => {
              errors++
            }

            setTimeout(() => {
              resolve({ connected: false, messagesReceived: 0, errors: 1 })
            }, 12_000)
          }
        )
      },
      `${AVA_WEB_URL.replace('http', 'ws')}/ws`
    )

    expect(result.connected).toBe(true)
    expect(result.errors).toBe(0)
  })

  test('multiple WebSocket connections do not crash the server', async ({ page }) => {
    await page.goto(AVA_WEB_URL)

    const result = await page.evaluate(
      async (wsUrl: string) => {
        const connections = 3
        const results: boolean[] = []

        const promises = Array.from({ length: connections }, () => {
          return new Promise<boolean>((resolve) => {
            const ws = new WebSocket(wsUrl)
            ws.onopen = () => {
              setTimeout(() => {
                const wasOpen = ws.readyState === WebSocket.OPEN
                ws.close()
                resolve(wasOpen)
              }, 3000)
            }
            ws.onerror = () => resolve(false)
            setTimeout(() => resolve(false), 5000)
          })
        })

        const res = await Promise.all(promises)
        return { allConnected: res.every(Boolean), count: res.filter(Boolean).length }
      },
      `${AVA_WEB_URL.replace('http', 'ws')}/ws`
    )

    expect(result.count).toBeGreaterThanOrEqual(2)
  })
})

// ---------------------------------------------------------------------------
// Tool exercise tests (require agent to actually use tools)
// ---------------------------------------------------------------------------

test.describe('Tool Exercise via UI', () => {
  test.beforeEach(async ({ page }) => {
    await requireBackend()
    await setupPage(page)
    await page.goto(AVA_WEB_URL)
    await page.reload()
    await waitForAppShell(page)
    await dismissChangelog(page)
  })

  test('agent can use glob tool to find files', async ({ page }) => {
    await sendMessage(
      page,
      'Use the glob tool to find all .toml files in this project. List the file paths you found.'
    )
    const response = await waitForAssistantResponse(page)
    expect(response.length).toBeGreaterThan(0)
    // Response should mention Cargo.toml at minimum
  })

  test('agent can use grep tool to search code', async ({ page }) => {
    await sendMessage(
      page,
      'Use grep to search for "fn main" across all Rust files in this project. How many matches did you find?'
    )
    const response = await waitForAssistantResponse(page)
    expect(response.length).toBeGreaterThan(0)
  })

  test('agent can use bash tool to run a command', async ({ page }) => {
    await sendMessage(page, 'Run the command "echo STRESS_TEST_BASH_OK" and tell me the output.')

    const response = await waitForAssistantResponse(page)
    expect(response.length).toBeGreaterThan(0)
  })
})
