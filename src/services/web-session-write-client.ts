import { buildApiUrl, withWebServerAuth } from '../lib/api-client'
import { buildSessionBaseEndpoint } from './web-session-identity'

export interface BrowserSessionWriteResult<T = undefined> {
  ok: boolean
  status: number
  statusText: string
  data?: T
  errorText?: string
}

interface BrowserSessionWriteOptions {
  method?: string
  jsonBody?: unknown
  parseJson?: boolean
}

function buildSessionCollectionWriteEndpoint(action: string): string {
  return buildApiUrl(`/api/sessions/${action}`)
}

function buildWriteInit(options: BrowserSessionWriteOptions): RequestInit {
  if (options.jsonBody === undefined) {
    return withWebServerAuth({ method: options.method || 'POST' })
  }

  return withWebServerAuth({
    method: options.method || 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(options.jsonBody),
  })
}

async function runBrowserSessionWrite<T>(
  url: string,
  options: BrowserSessionWriteOptions
): Promise<BrowserSessionWriteResult<T>> {
  let response: Response

  try {
    response = await fetch(url, buildWriteInit(options))
  } catch (error) {
    return {
      ok: false,
      status: 0,
      statusText: 'Network Error',
      errorText: error instanceof Error ? error.message : String(error),
    }
  }

  if (!response.ok) {
    return {
      ok: false,
      status: response.status,
      statusText: response.statusText,
      errorText: await response.text(),
    }
  }

  return {
    ok: true,
    status: response.status,
    statusText: response.statusText,
    data: options.parseJson ? ((await response.json()) as T) : undefined,
  }
}

export async function writeBrowserSessionCollection<T = undefined>(
  options: {
    action: string
  } & BrowserSessionWriteOptions
): Promise<BrowserSessionWriteResult<T>> {
  return runBrowserSessionWrite<T>(buildSessionCollectionWriteEndpoint(options.action), options)
}

export async function writeBrowserSession<T = undefined>(
  options: {
    frontendSessionId: string
    action?: string
  } & BrowserSessionWriteOptions
): Promise<BrowserSessionWriteResult<T>> {
  return runBrowserSessionWrite<T>(
    buildSessionBaseEndpoint(options.frontendSessionId, options.action),
    options
  )
}
