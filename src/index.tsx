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
import { logError as fileLogError, logFatal } from './services/logger'

if (typeof window !== 'undefined') {
  window.addEventListener('error', (e) => {
    const stack = e.error instanceof Error ? e.error.stack : undefined
    logFatal('Uncaught', e.error instanceof Error ? e.error.message : String(e.error), stack)
  })
  window.addEventListener('unhandledrejection', (e) => {
    const reason = e.reason instanceof Error ? e.reason.message : String(e.reason)
    const stack = e.reason instanceof Error ? e.reason.stack : undefined
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
  <div class="flex h-screen items-center justify-center bg-[var(--background)]">
    <div class="text-center">
      <div class="animate-spin rounded-full h-12 w-12 border-b-2 border-[var(--accent)] mx-auto" />
      <p class="mt-4 text-[var(--text-secondary)]">Loading...</p>
    </div>
  </div>
)

render(
  () => (
    <ThemeProvider>
      <NotificationProvider position="top-right">
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
