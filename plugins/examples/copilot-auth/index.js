#!/usr/bin/env node
// copilot-auth — GitHub Copilot authentication plugin for AVA.
//
// Implements the device code OAuth flow used by GitHub Copilot:
//   1. Request a device code from GitHub
//   2. User visits https://github.com/login/device and enters the code
//   3. Poll GitHub for the OAuth access token
//   4. Exchange the OAuth token for a short-lived Copilot API token
//   5. Cache and refresh the Copilot token as needed
//
// Protocol: JSON-RPC 2.0 over stdio with Content-Length framing.
// No npm dependencies required — uses only Node.js built-ins.

const fs = require('node:fs')
const https = require('node:https')
const { URL } = require('node:url')

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const PLUGIN_NAME = 'copilot-auth'

// Public OAuth client ID used by VS Code Copilot.
const GITHUB_CLIENT_ID = 'Iv1.b507a08c87ecfe98'

// GitHub endpoints
const DEVICE_CODE_URL = 'https://github.com/login/device/code'
const TOKEN_URL = 'https://github.com/login/oauth/access_token'
const COPILOT_TOKEN_URL = 'https://api.github.com/copilot_internal/v2/token'

// Default Copilot API endpoint (individual plan).
const DEFAULT_COPILOT_ENDPOINT = 'https://api.individual.githubcopilot.com'

// Allowed Copilot API host suffixes (for endpoint validation).
const ALLOWED_HOSTS = ['api.github.com', 'githubcopilot.com', 'githubusercontent.com']

// Token safety margin: refresh 60s before expiry.
const TOKEN_SAFETY_MARGIN_SECS = 60

// Maximum time to wait for user to authorize (5 minutes).
const MAX_POLL_TIMEOUT_MS = 5 * 60 * 1000

// Hooks this plugin handles.
const HOOKS = ['auth', 'auth.authorize', 'auth.refresh', 'session.start']

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

let context = null

// Cached tokens.
let githubAccessToken = null // long-lived GitHub OAuth token
let copilotToken = null // { token, expires_at, api_endpoint }

// ---------------------------------------------------------------------------
// JSON-RPC wire helpers (Content-Length framing, same as tool-timer)
// ---------------------------------------------------------------------------

function sendMessage(msg) {
  const json = JSON.stringify(msg)
  const header = `Content-Length: ${Buffer.byteLength(json)}\r\n\r\n`
  fs.writeSync(1, header + json)
}

function sendResult(id, result) {
  sendMessage({ jsonrpc: '2.0', id, result: result ?? null })
}

function sendError(id, code, message, data) {
  const error = { code, message }
  if (data !== undefined) error.data = data
  sendMessage({ jsonrpc: '2.0', id, error })
}

function log(msg) {
  process.stderr.write(`[${PLUGIN_NAME}] ${msg}\n`)
}

// ---------------------------------------------------------------------------
// HTTPS request helper (zero-dependency, returns a Promise)
// ---------------------------------------------------------------------------

function httpsRequest(url, options = {}) {
  return new Promise((resolve, reject) => {
    const parsed = new URL(url)
    const reqOptions = {
      hostname: parsed.hostname,
      port: parsed.port || 443,
      path: parsed.pathname + parsed.search,
      method: options.method || 'GET',
      headers: {
        Accept: 'application/json',
        'User-Agent': 'AVA-CopilotAuth-Plugin/0.1.0',
        ...(options.headers || {}),
      },
    }

    const req = https.request(reqOptions, (res) => {
      const chunks = []
      res.on('data', (chunk) => chunks.push(chunk))
      res.on('end', () => {
        const body = Buffer.concat(chunks).toString('utf-8')
        let parsed = body
        try {
          parsed = JSON.parse(body)
        } catch {
          // Keep as string if not valid JSON.
        }
        resolve({ status: res.statusCode, headers: res.headers, body: parsed })
      })
    })

    req.on('error', (err) => reject(err))
    req.setTimeout(30_000, () => {
      req.destroy(new Error('Request timed out'))
    })

    if (options.body) {
      req.write(options.body)
    }
    req.end()
  })
}

// ---------------------------------------------------------------------------
// URL-encoded form body helper
// ---------------------------------------------------------------------------

function formEncode(params) {
  return Object.entries(params)
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
    .join('&')
}

// ---------------------------------------------------------------------------
// Copilot endpoint validation
// ---------------------------------------------------------------------------

function validateCopilotEndpoint(endpoint) {
  let url
  try {
    url = new URL(endpoint)
  } catch {
    return false
  }
  const host = url.hostname
  return ALLOWED_HOSTS.some((allowed) => host === allowed || host.endsWith(`.${allowed}`))
}

// Extract API endpoint from a Copilot token string.
// Token format contains "proxy-ep=proxy.individual.githubcopilot.com".
function extractEndpointFromToken(token) {
  const parts = token.split(';')
  const proxyPart = parts.find((p) => p.startsWith('proxy-ep='))
  if (!proxyPart) return null

  const proxyEp = proxyPart.slice('proxy-ep='.length)
  if (!proxyEp) return null

  const apiHost = proxyEp.startsWith('proxy.') ? `api.${proxyEp.slice('proxy.'.length)}` : proxyEp

  return `https://${apiHost}`
}

// ---------------------------------------------------------------------------
// Device code flow
// ---------------------------------------------------------------------------

async function requestDeviceCode() {
  const body = formEncode({
    client_id: GITHUB_CLIENT_ID,
    scope: 'read:user',
  })

  const res = await httpsRequest(DEVICE_CODE_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
  })

  if (res.status !== 200) {
    throw new Error(
      `Device code request failed (${res.status}): ${JSON.stringify(res.body).slice(0, 500)}`
    )
  }

  const data = res.body
  if (typeof data !== 'object' || !data.device_code) {
    throw new Error(`Unexpected device code response: ${JSON.stringify(data).slice(0, 500)}`)
  }

  return {
    device_code: data.device_code,
    user_code: data.user_code,
    verification_uri: data.verification_uri,
    expires_in: data.expires_in || 900,
    interval: data.interval || 5,
  }
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

async function pollForToken(deviceCode, interval, expiresIn) {
  const deadline = Date.now() + Math.min(expiresIn * 1000, MAX_POLL_TIMEOUT_MS)
  let currentInterval = interval

  while (Date.now() < deadline) {
    await sleep(currentInterval * 1000)

    const body = formEncode({
      client_id: GITHUB_CLIENT_ID,
      device_code: deviceCode,
      grant_type: 'urn:ietf:params:oauth:grant-type:device_code',
    })

    let res
    try {
      res = await httpsRequest(TOKEN_URL, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body,
      })
    } catch (err) {
      log(`Poll request failed: ${err.message}`)
      continue
    }

    const data = res.body
    if (typeof data !== 'object') continue

    if (data.access_token) {
      return data.access_token
    }

    switch (data.error) {
      case 'authorization_pending':
        // Still waiting for user.
        continue
      case 'slow_down':
        currentInterval += 5
        continue
      case 'expired_token':
        return null
      case 'access_denied':
        throw new Error('User denied the authorization request')
      default:
        if (data.error) {
          throw new Error(`OAuth error: ${data.error} — ${data.error_description || ''}`)
        }
        continue
    }
  }

  return null // Timed out
}

// ---------------------------------------------------------------------------
// Copilot token exchange
// ---------------------------------------------------------------------------

async function exchangeCopilotToken(accessToken) {
  const res = await httpsRequest(COPILOT_TOKEN_URL, {
    method: 'GET',
    headers: {
      Authorization: `token ${accessToken}`,
      'User-Agent': 'GitHubCopilotChat/0.35.0',
      'Editor-Version': 'vscode/1.107.0',
      'Editor-Plugin-Version': 'copilot-chat/0.35.0',
      'Copilot-Integration-Id': 'vscode-chat',
    },
  })

  if (res.status !== 200) {
    throw new Error(
      `Copilot token exchange failed (${res.status}): ${JSON.stringify(res.body).slice(0, 500)}`
    )
  }

  const data = res.body
  if (typeof data !== 'object' || !data.token) {
    throw new Error(`Unexpected Copilot token response: ${JSON.stringify(data).slice(0, 500)}`)
  }

  // Resolve API endpoint: response > token string > default
  let apiEndpoint =
    data.endpoints?.api || extractEndpointFromToken(data.token) || DEFAULT_COPILOT_ENDPOINT

  if (!validateCopilotEndpoint(apiEndpoint)) {
    log(`Untrusted Copilot endpoint "${apiEndpoint}", falling back to default`)
    apiEndpoint = DEFAULT_COPILOT_ENDPOINT
  }

  return {
    token: data.token,
    expires_at: data.expires_at,
    api_endpoint: apiEndpoint,
  }
}

function isCopilotTokenExpired() {
  if (!copilotToken) return true
  const now = Math.floor(Date.now() / 1000)
  return now + TOKEN_SAFETY_MARGIN_SECS >= copilotToken.expires_at
}

// ---------------------------------------------------------------------------
// Hook handlers
// ---------------------------------------------------------------------------

function isTargetProvider(params) {
  const provider = (params.provider || '').toLowerCase()
  return provider === 'copilot' || provider === 'github-copilot'
}

// hook/auth — report auth method info for this provider.
function handleAuth(params) {
  if (!isTargetProvider(params)) {
    return { handled: false }
  }

  return {
    handled: true,
    provider: 'copilot',
    method: 'device_code',
    description: 'GitHub Copilot authentication via device code flow',
    device_code_url: DEVICE_CODE_URL,
    verification_uri: 'https://github.com/login/device',
    has_cached_token: !!githubAccessToken,
    copilot_token_valid: !isCopilotTokenExpired(),
  }
}

// hook/auth.authorize — perform the full device code flow + token exchange.
async function handleAuthAuthorize(params) {
  if (!isTargetProvider(params)) {
    return { handled: false }
  }

  // If we already have a valid Copilot token, return it.
  if (!isCopilotTokenExpired()) {
    log('Returning cached Copilot token')
    return {
      handled: true,
      credentials: {
        api_key: copilotToken.token,
        base_url: copilotToken.api_endpoint,
        expires_at: copilotToken.expires_at,
      },
    }
  }

  // If we have a GitHub token, try exchanging it first.
  if (githubAccessToken) {
    try {
      copilotToken = await exchangeCopilotToken(githubAccessToken)
      log(`Copilot token obtained (expires at ${copilotToken.expires_at})`)
      return {
        handled: true,
        credentials: {
          api_key: copilotToken.token,
          base_url: copilotToken.api_endpoint,
          expires_at: copilotToken.expires_at,
        },
      }
    } catch (err) {
      log(`Cached GitHub token failed: ${err.message}`)
      githubAccessToken = null
    }
  }

  // Full device code flow.
  log('Starting device code flow...')
  let deviceCodeResp
  try {
    deviceCodeResp = await requestDeviceCode()
  } catch (err) {
    throw new Error(`Failed to start device code flow: ${err.message}`)
  }

  log(`User code: ${deviceCodeResp.user_code}`)
  log(`Visit: ${deviceCodeResp.verification_uri}`)
  log(`Code expires in ${deviceCodeResp.expires_in}s`)

  // Notify caller about the user action needed.
  // The caller (AVA) should display this to the user.
  // We continue polling in-process.

  let accessToken
  try {
    accessToken = await pollForToken(
      deviceCodeResp.device_code,
      deviceCodeResp.interval,
      deviceCodeResp.expires_in
    )
  } catch (err) {
    throw new Error(`Device code polling failed: ${err.message}`)
  }

  if (!accessToken) {
    throw new Error('Device code expired or timed out. Please try again.')
  }

  githubAccessToken = accessToken
  log('GitHub OAuth token obtained, exchanging for Copilot token...')

  try {
    copilotToken = await exchangeCopilotToken(githubAccessToken)
  } catch (err) {
    throw new Error(`Copilot token exchange failed: ${err.message}`)
  }

  log(`Copilot token obtained (endpoint: ${copilotToken.api_endpoint})`)

  return {
    handled: true,
    user_code: deviceCodeResp.user_code,
    verification_uri: deviceCodeResp.verification_uri,
    credentials: {
      api_key: copilotToken.token,
      base_url: copilotToken.api_endpoint,
      expires_at: copilotToken.expires_at,
    },
  }
}

// hook/auth.refresh — refresh an expired Copilot token.
async function handleAuthRefresh(params) {
  if (!isTargetProvider(params)) {
    return { handled: false }
  }

  if (!githubAccessToken) {
    throw new Error(
      'No GitHub OAuth token cached. Run auth.authorize first to perform the device code flow.'
    )
  }

  if (!isCopilotTokenExpired()) {
    return {
      handled: true,
      refreshed: false,
      reason: 'Token still valid',
      credentials: {
        api_key: copilotToken.token,
        base_url: copilotToken.api_endpoint,
        expires_at: copilotToken.expires_at,
      },
    }
  }

  log('Refreshing Copilot token...')
  try {
    copilotToken = await exchangeCopilotToken(githubAccessToken)
  } catch (err) {
    // GitHub token may have been revoked. Clear state.
    githubAccessToken = null
    copilotToken = null
    throw new Error(
      `Token refresh failed (GitHub token may be revoked): ${err.message}. Re-run auth.authorize.`
    )
  }

  log(`Copilot token refreshed (expires at ${copilotToken.expires_at})`)
  return {
    handled: true,
    refreshed: true,
    credentials: {
      api_key: copilotToken.token,
      base_url: copilotToken.api_endpoint,
      expires_at: copilotToken.expires_at,
    },
  }
}

// hook/session.start — log that the plugin is active.
function handleSessionStart() {
  log('Copilot auth plugin active')
  if (githubAccessToken) {
    log(`GitHub token cached, Copilot token ${isCopilotTokenExpired() ? 'expired' : 'valid'}`)
  } else {
    log('No cached tokens — auth.authorize required before use')
  }
  return {}
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------

async function handleMessage(msg) {
  // --- initialize ---
  if (msg.method === 'initialize') {
    context = msg.params || {}
    log('Plugin initialized')

    // Check if a GitHub token was provided in config (pre-seeded credentials).
    if (context.config?.github_token) {
      githubAccessToken = context.config.github_token
      log('Using pre-seeded GitHub token from config')
    }

    sendResult(msg.id, { hooks: HOOKS })
    return
  }

  // --- shutdown ---
  if (msg.method === 'shutdown') {
    log('Shutting down')
    sendResult(msg.id, null)
    process.exit(0)
  }

  // --- hook dispatch ---
  if (msg.method?.startsWith('hook/')) {
    const hook = msg.method.slice(5)
    const params = msg.params || {}

    try {
      let result
      switch (hook) {
        case 'auth':
          result = handleAuth(params)
          break
        case 'auth.authorize':
          result = await handleAuthAuthorize(params)
          break
        case 'auth.refresh':
          result = await handleAuthRefresh(params)
          break
        case 'session.start':
          result = handleSessionStart()
          break
        default:
          if (msg.id != null) sendError(msg.id, -32601, `No handler for hook '${hook}'`)
          return
      }
      if (msg.id != null) sendResult(msg.id, result)
    } catch (err) {
      log(`Error in hook/${hook}: ${err.message}`)
      if (msg.id != null) sendError(msg.id, -32000, err.message)
    }
    return
  }

  // --- unknown method ---
  if (msg.id != null) sendResult(msg.id, {})
}

// ---------------------------------------------------------------------------
// stdio JSON-RPC framing (Content-Length)
// ---------------------------------------------------------------------------

let buffer = ''
process.stdin.setEncoding('utf-8')
process.stdin.on('data', (chunk) => {
  buffer += chunk
  while (true) {
    const headerEnd = buffer.indexOf('\r\n\r\n')
    if (headerEnd === -1) break
    const header = buffer.substring(0, headerEnd)
    const match = header.match(/Content-Length:\s*(\d+)/i)
    if (!match) {
      buffer = buffer.substring(headerEnd + 4)
      continue
    }
    const len = parseInt(match[1], 10)
    const bodyStart = headerEnd + 4
    if (buffer.length < bodyStart + len) break
    const body = buffer.substring(bodyStart, bodyStart + len)
    buffer = buffer.substring(bodyStart + len)
    try {
      handleMessage(JSON.parse(body)).catch((err) => {
        log(`Unhandled error: ${err.message}`)
      })
    } catch (e) {
      log(`Parse error: ${e.message}`)
    }
  }
})

process.stdin.resume()
