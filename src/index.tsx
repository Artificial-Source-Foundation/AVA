/* @refresh reload */

import { lazy, Suspense } from 'solid-js'
import { render } from 'solid-js/web'
import './index.css'
import { ThemeProvider } from './contexts/theme'

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
      <Suspense fallback={<LoadingFallback />}>
        {isPreviewMode() ? <DesignSystemPreview /> : <App />}
      </Suspense>
    </ThemeProvider>
  ),
  document.getElementById('root') as HTMLElement
)
