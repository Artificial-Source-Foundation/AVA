/**
 * App Shell - Main Layout Container
 *
 * Premium layout with sidebar, tab navigation, main content, and status bar.
 * Uses design system tokens for consistent theming.
 */

import type { Component } from 'solid-js'
import { MainContent } from './MainContent'
import { Sidebar } from './Sidebar'
import { StatusBar } from './StatusBar'
import { TabBar } from './TabBar'

export const AppShell: Component = () => {
  return (
    <div class="flex h-screen bg-[var(--background)] text-[var(--text-primary)] overflow-hidden">
      {/* Sidebar */}
      <Sidebar />

      {/* Main content area */}
      <div class="flex flex-1 flex-col min-w-0">
        {/* Tab navigation */}
        <TabBar />

        {/* Main content (tab router) */}
        <main class="flex-1 overflow-hidden relative">
          <MainContent />
        </main>

        {/* Status bar */}
        <StatusBar />
      </div>
    </div>
  )
}
