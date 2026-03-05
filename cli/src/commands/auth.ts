/**
 * AVA CLI Auth Command
 * Handles OAuth authentication for LLM providers
 */

import * as readline from 'node:readline'
import {
  completeOAuthFlow,
  getAuthStatus,
  type OAuthProvider,
  removeStoredAuth,
  startOAuthFlow,
} from '../auth/index.js'
import { getCliLogger } from '../logger.js'

const SUPPORTED_PROVIDERS: OAuthProvider[] = ['openai', 'google', 'copilot']
const log = getCliLogger('cli:auth')

/**
 * Run the auth command
 */
export async function runAuthCommand(args: string[]): Promise<void> {
  const subcommand = args[0]
  const provider = args[1] as OAuthProvider | undefined
  log.info('Auth command invoked', {
    subcommand: subcommand ?? 'none',
    provider: provider ?? 'none',
  })

  switch (subcommand) {
    case 'login':
      if (!provider || !SUPPORTED_PROVIDERS.includes(provider)) {
        console.log(`Usage: ava auth login <provider>`)
        console.log(`Providers: ${SUPPORTED_PROVIDERS.join(', ')}`)
        return
      }
      await loginProvider(provider)
      break

    case 'logout':
      if (!provider || !SUPPORTED_PROVIDERS.includes(provider)) {
        console.log(`Usage: ava auth logout <provider>`)
        console.log(`Providers: ${SUPPORTED_PROVIDERS.join(', ')}`)
        return
      }
      await logoutProvider(provider)
      break

    case 'status':
      await showAuthStatus()
      break

    default:
      printHelp()
  }
}

/**
 * Login to a provider via OAuth
 */
async function loginProvider(provider: OAuthProvider): Promise<void> {
  console.log(`\nConnecting to ${getProviderName(provider)}...`)
  log.info('Auth login started', { provider })

  try {
    const authResult = await startOAuthFlow(provider)
    log.info('OAuth flow started', { provider, method: authResult.method })

    console.log(`\n🔗 Open this URL in your browser:\n`)
    console.log(`   ${authResult.url}\n`)
    console.log(`📋 ${authResult.instructions}\n`)

    if (authResult.method === 'code') {
      // Need to prompt for code
      const code = await promptForInput('Paste the authorization code: ')

      console.log('\nExchanging code for tokens...')
      const result = await authResult.callback(code)

      if (result.type === 'success') {
        await completeOAuthFlow(provider, result)
        log.info('Auth login completed', { provider, method: 'code' })
        console.log(`\n✅ Successfully connected to ${getProviderName(provider)}!`)
      } else {
        log.warn('Auth login failed', { provider, method: 'code', error: result.error })
        console.error(`\n❌ Authentication failed: ${result.error}`)
      }
    } else {
      // Auto method - wait for callback
      console.log('Waiting for authorization...')
      const result = await authResult.callback()

      if (result.type === 'success') {
        await completeOAuthFlow(provider, result)
        log.info('Auth login completed', { provider, method: 'auto' })
        console.log(`\n✅ Successfully connected to ${getProviderName(provider)}!`)
      } else {
        log.warn('Auth login failed', { provider, method: 'auto', error: result.error })
        console.error(`\n❌ Authentication failed: ${result.error}`)
      }
    }
  } catch (error) {
    log.error('Auth login crashed', {
      provider,
      error: error instanceof Error ? error.message : 'Unknown error',
    })
    console.error(`\n❌ Error: ${error instanceof Error ? error.message : 'Unknown error'}`)
  }
}

/**
 * Logout from a provider
 */
async function logoutProvider(provider: OAuthProvider): Promise<void> {
  await removeStoredAuth(provider)
  log.info('Auth logout completed', { provider })
  console.log(`\n✅ Disconnected from ${getProviderName(provider)}`)
}

/**
 * Show auth status for all providers
 */
async function showAuthStatus(): Promise<void> {
  console.log('\nAuthentication Status:\n')

  for (const provider of SUPPORTED_PROVIDERS) {
    const status = await getAuthStatus(provider)
    const name = getProviderName(provider)

    if (status.isAuthenticated) {
      const authTypeLabel = status.authType === 'oauth' ? 'OAuth' : 'API Key'
      let expiryInfo = ''
      if (status.expiresAt) {
        const expiresIn = Math.round((status.expiresAt - Date.now()) / 1000 / 60)
        expiryInfo = expiresIn > 0 ? ` (expires in ${expiresIn} min)` : ' (expired)'
      }
      console.log(`  ✅ ${name}: ${authTypeLabel}${expiryInfo}`)
    } else {
      console.log(`  ❌ ${name}: Not authenticated`)
    }
  }

  console.log('')
}

/**
 * Print help message
 */
function printHelp(): void {
  console.log(`
AVA Auth - Manage authentication for LLM providers

USAGE:
  ava auth <command> [provider]

COMMANDS:
  login <provider>   Connect using OAuth (consumer subscriptions)
  logout <provider>  Disconnect and remove stored credentials
  status             Show authentication status for all providers

PROVIDERS:
  openai      ChatGPT Plus/Pro subscription via OpenAI Codex
  google      Gemini via Google Antigravity
  copilot     GitHub Copilot subscription

EXAMPLES:
  ava auth login openai       # Connect ChatGPT subscription
  ava auth login google       # Connect Gemini/Antigravity
  ava auth login copilot      # Connect GitHub Copilot
  ava auth status             # Check auth status

NOTE:
  You can also use API keys via environment variables:
    AVA_ANTHROPIC_API_KEY
    AVA_OPENAI_API_KEY
    AVA_GOOGLE_API_KEY
`)
}

/**
 * Get human-readable provider name
 */
function getProviderName(provider: OAuthProvider): string {
  const names: Record<OAuthProvider, string> = {
    openai: 'ChatGPT (OpenAI)',
    google: 'Gemini (Google Antigravity)',
    copilot: 'GitHub Copilot',
  }
  return names[provider] ?? provider
}

/**
 * Prompt for user input
 */
function promptForInput(prompt: string): Promise<string> {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
  })

  return new Promise((resolve) => {
    rl.question(prompt, (answer) => {
      rl.close()
      resolve(answer.trim())
    })
  })
}
