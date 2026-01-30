/**
 * ErrorBoundary Component
 * Catches and displays errors in child components
 */

import { type Component, type ParentComponent, ErrorBoundary as SolidErrorBoundary } from 'solid-js'

interface ErrorFallbackProps {
  error: Error
  reset: () => void
}

const ErrorFallback: Component<ErrorFallbackProps> = (props) => {
  return (
    <div class="p-4 bg-red-900/30 border border-red-700 rounded-lg">
      <div class="flex items-start gap-3">
        <div class="flex-shrink-0 text-red-400">
          <svg
            class="w-6 h-6"
            fill="none"
            stroke="currentColor"
            viewBox="0 0 24 24"
            role="img"
            aria-label="Error"
          >
            <path
              stroke-linecap="round"
              stroke-linejoin="round"
              stroke-width="2"
              d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z"
            />
          </svg>
        </div>
        <div class="flex-1">
          <h3 class="text-red-300 font-medium">Something went wrong</h3>
          <p class="text-red-400 text-sm mt-1">{props.error.message}</p>
          <div class="mt-3 flex gap-2">
            <button
              type="button"
              onClick={props.reset}
              class="px-3 py-1.5 bg-red-600 hover:bg-red-500 text-white rounded text-sm"
            >
              Try again
            </button>
            <button
              type="button"
              onClick={() => window.location.reload()}
              class="px-3 py-1.5 bg-gray-600 hover:bg-gray-500 text-white rounded text-sm"
            >
              Reload page
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}

interface ErrorBoundaryProps {
  fallback?: Component<ErrorFallbackProps>
}

export const ErrorBoundary: ParentComponent<ErrorBoundaryProps> = (props) => {
  return (
    <SolidErrorBoundary
      fallback={(err, reset) => {
        const FallbackComponent = props.fallback || ErrorFallback
        return <FallbackComponent error={err} reset={reset} />
      }}
    >
      {props.children}
    </SolidErrorBoundary>
  )
}
