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
  <div class="fixed inset-0 z-[9999] flex flex-col items-center justify-center bg-[var(--background)]">
    <div class="splash-logo mb-6">
      <svg
        width="64"
        height="64"
        viewBox="0 0 64 64"
        fill="none"
        xmlns="http://www.w3.org/2000/svg"
        aria-hidden="true"
      >
        <path
          d="M32 4L58 32L32 60L6 32L32 4Z"
          stroke="var(--accent)"
          stroke-width="2.5"
          fill="none"
          opacity="0.3"
        />
        <path
          d="M32 12L50 32L32 52L14 32L32 12Z"
          stroke="var(--accent)"
          stroke-width="2"
          fill="var(--accent)"
          opacity="0.15"
        />
        <path d="M32 20L42 32L32 44L22 32L32 20Z" fill="var(--accent)" opacity="0.6" />
      </svg>
    </div>
    <h1 class="text-xl font-semibold tracking-widest uppercase text-[var(--text-primary)] mb-8">
      Estela
    </h1>
    <div class="flex gap-1.5">
      <span
        class="splash-dot w-1.5 h-1.5 rounded-full bg-[var(--accent)]"
        style="animation-delay: 0ms"
      />
      <span
        class="splash-dot w-1.5 h-1.5 rounded-full bg-[var(--accent)]"
        style="animation-delay: 150ms"
      />
      <span
        class="splash-dot w-1.5 h-1.5 rounded-full bg-[var(--accent)]"
        style="animation-delay: 300ms"
      />
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
