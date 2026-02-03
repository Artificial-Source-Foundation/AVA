/**
 * App Shell - Main Layout Container
 *
 * Premium layout with sidebar, main content, and status bar.
 * Uses design system tokens for consistent theming.
 */

import type { ParentComponent } from 'solid-js'
import { Sidebar } from './Sidebar'
import { StatusBar } from './StatusBar'

export const AppShell: ParentComponent = (props) => {
  return (
    <div class="flex h-screen bg-[var(--background)] text-[var(--text-primary)] overflow-hidden">
      {/* Sidebar */}
      <Sidebar />

      {/* Main content area */}
      <div class="flex flex-1 flex-col min-w-0">
        {/* Main content */}
        <main class="flex-1 overflow-hidden relative">{props.children}</main>

        {/* Status bar */}
        <StatusBar />
      </div>
    </div>
  )
}
