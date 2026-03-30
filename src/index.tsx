/* @refresh reload */

import { lazy, Suspense } from 'solid-js'
import { render } from 'solid-js/web'
import { AppErrorBoundary } from './components/ErrorBoundary'
import { NotificationProvider } from './contexts/notification'
import { ThemeProvider } from './contexts/theme'
import './index.css'

// Global error handlers — writes to log file via Tauri FS + console
// Logger must be initialized async (after Tauri is ready), so early errors
// still get buffered and flushed once initLogger() is called from App.tsx
import { log } from './lib/logger'
import { logError as fileLogError, logFatal } from './services/logger'

if (typeof window !== 'undefined') {
  window.addEventListener('error', (e) => {
    const msg = e.error instanceof Error ? e.error.message : String(e.error)
    const stack = e.error instanceof Error ? e.error.stack : undefined
    log.error('error', `Uncaught error: ${msg}`, { stack })
    logFatal('Uncaught', msg, stack)
  })
  window.addEventListener('unhandledrejection', (e) => {
    const reason = e.reason instanceof Error ? e.reason.message : String(e.reason)
    const stack = e.reason instanceof Error ? e.reason.stack : undefined
    log.error('error', `Unhandled promise rejection: ${reason}`, { stack })
    fileLogError('UnhandledPromise', reason, stack)
  })
  // Disable Tauri's default context menu globally
  document.addEventListener('contextmenu', (e) => {
    e.preventDefault()
  })
}

// Check if we're in preview mode BEFORE importing App
// This prevents Node.js-only dependencies from being loaded in the browser
const isPreviewMode = () => {
  if (typeof window === 'undefined') return false
  const params = new URLSearchParams(window.location.search)
  return params.get('preview') === 'true'
}

// Lazy load components to avoid importing Node.js code in preview mode
const App = lazy(() => import('./App'))
const DesignSystemPreview = lazy(() =>
  import('./pages/DesignSystemPreview').then((m) => ({ default: m.DesignSystemPreview }))
)

const LoadingFallback = () => (
  <div
    class="fixed inset-0 z-[9999] flex items-center justify-center"
    style={{ background: '#0A0A0C' }}
  >
    <div
      class="splash-logo"
      style={{
        width: '64px',
        height: '64px',
        'border-radius': '16px',
        background: 'linear-gradient(180deg, #0A84FF 0%, #5E5CE6 100%)',
        display: 'flex',
        'align-items': 'center',
        'justify-content': 'center',
        'box-shadow': '0 0 80px #0A84FF10',
      }}
    >
      <span
        style={{
          color: '#FFFFFF',
          'font-family': 'Geist, system-ui, sans-serif',
          'font-size': '28px',
          'font-weight': '800',
          'line-height': '1',
          'user-select': 'none',
        }}
      >
        A
      </span>
    </div>
    <span
      style={{
        position: 'absolute',
        bottom: '30px',
        left: '50%',
        transform: 'translateX(-50%)',
        color: '#1C1C1E',
        'font-family': '"Geist Mono", ui-monospace, monospace',
        'font-size': '11px',
        'letter-spacing': '4px',
        'user-select': 'none',
      }}
    >
      ava
    </span>
  </div>
)

render(
  () => (
    <ThemeProvider>
      <NotificationProvider position="top-center">
        <AppErrorBoundary>
          <Suspense fallback={<LoadingFallback />}>
            {isPreviewMode() ? <DesignSystemPreview /> : <App />}
          </Suspense>
        </AppErrorBoundary>
      </NotificationProvider>
    </ThemeProvider>
  ),
  document.getElementById('root') as HTMLElement
)
