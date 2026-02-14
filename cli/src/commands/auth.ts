/**
 * Estela CLI Auth Command
 * Handles OAuth authentication for LLM providers
 */

import * as readline from 'node:readline'
import {
  completeOAuthFlow,
  getAuthStatus,
  type OAuthProvider,
  removeStoredAuth,
  startOAuthFlow,
} from '@ava/core'

const SUPPORTED_PROVIDERS: OAuthProvider[] = ['anthropic', 'openai', 'google', 'copilot']

/**
 * Run the auth command
 */
export async function runAuthCommand(args: string[]): Promise<void> {
  const subcommand = args[0]
  const provider = args[1] as OAuthProvider | undefined

  switch (subcommand) {
    case 'login':
      if (!provider || !SUPPORTED_PROVIDERS.includes(provider)) {
        console.log(`Usage: estela auth login <provider>`)
        console.log(`Providers: ${SUPPORTED_PROVIDERS.join(', ')}`)
        return
      }
      await loginProvider(provider)
      break

    case 'logout':
      if (!provider || !SUPPORTED_PROVIDERS.includes(provider)) {
        console.log(`Usage: estela auth logout <provider>`)
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

  try {
    const authResult = await startOAuthFlow(provider)

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
        console.log(`\n✅ Successfully connected to ${getProviderName(provider)}!`)
      } else {
        console.error(`\n❌ Authentication failed: ${result.error}`)
      }
    } else {
      // Auto method - wait for callback
      console.log('Waiting for authorization...')
      const result = await authResult.callback()

      if (result.type === 'success') {
        await completeOAuthFlow(provider, result)
        console.log(`\n✅ Successfully connected to ${getProviderName(provider)}!`)
      } else {
        console.error(`\n❌ Authentication failed: ${result.error}`)
      }
    }
  } catch (error) {
    console.error(`\n❌ Error: ${error instanceof Error ? error.message : 'Unknown error'}`)
  }
}

/**
 * Logout from a provider
 */
async function logoutProvider(provider: OAuthProvider): Promise<void> {
  await removeStoredAuth(provider)
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
Estela Auth - Manage authentication for LLM providers

USAGE:
  estela auth <command> [provider]

COMMANDS:
  login <provider>   Connect using OAuth (consumer subscriptions)
  logout <provider>  Disconnect and remove stored credentials
  status             Show authentication status for all providers

PROVIDERS:
  anthropic   Claude Pro/Max subscription via claude.ai
  openai      ChatGPT Plus/Pro subscription via OpenAI Codex
  google      Gemini via Google Antigravity
  copilot     GitHub Copilot subscription

EXAMPLES:
  estela auth login anthropic    # Connect Claude subscription
  estela auth login openai       # Connect ChatGPT subscription
  estela auth login google       # Connect Gemini/Antigravity
  estela auth login copilot      # Connect GitHub Copilot
  estela auth status             # Check auth status
  estela auth logout anthropic   # Disconnect Claude

NOTE:
  You can also use API keys via environment variables:
    ESTELA_ANTHROPIC_API_KEY
    ESTELA_OPENAI_API_KEY
    ESTELA_GOOGLE_API_KEY
`)
}

/**
 * Get human-readable provider name
 */
function getProviderName(provider: OAuthProvider): string {
  const names: Record<OAuthProvider, string> = {
    anthropic: 'Claude (Anthropic)',
    openai: 'ChatGPT (OpenAI)',
    google: 'Gemini (Google Antigravity)',
    copilot: 'GitHub Copilot',
  }
  return names[provider]
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
