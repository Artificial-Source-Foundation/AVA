/**
 * Theme Context for Estela
 *
 * Provides theme (glass, minimal, terminal, soft) and mode (light, dark) management
 * with localStorage persistence and system preference detection.
 */

import {
  type Accessor,
  createContext,
  createEffect,
  createSignal,
  onMount,
  type ParentComponent,
  useContext,
} from 'solid-js'

export type Theme = 'glass' | 'minimal' | 'terminal' | 'soft'
export type Mode = 'light' | 'dark' | 'system'

interface ThemeContextValue {
  theme: Accessor<Theme>
  mode: Accessor<Mode>
  resolvedMode: Accessor<'light' | 'dark'>
  setTheme: (theme: Theme) => void
  setMode: (mode: Mode) => void
  toggleMode: () => void
}

const ThemeContext = createContext<ThemeContextValue>()

const THEME_STORAGE_KEY = 'estela-theme'
const MODE_STORAGE_KEY = 'estela-mode'

function getSystemMode(): 'light' | 'dark' {
  if (typeof window === 'undefined') return 'dark'
  return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
}

function getStoredTheme(): Theme {
  if (typeof localStorage === 'undefined') return 'glass'
  const stored = localStorage.getItem(THEME_STORAGE_KEY)
  if (stored === 'glass' || stored === 'minimal' || stored === 'terminal' || stored === 'soft') {
    return stored
  }
  return 'glass'
}

function getStoredMode(): Mode {
  if (typeof localStorage === 'undefined') return 'system'
  const stored = localStorage.getItem(MODE_STORAGE_KEY)
  if (stored === 'light' || stored === 'dark' || stored === 'system') {
    return stored
  }
  return 'system'
}

export const ThemeProvider: ParentComponent = (props) => {
  const [theme, setThemeState] = createSignal<Theme>(getStoredTheme())
  const [mode, setModeState] = createSignal<Mode>(getStoredMode())
  const [systemMode, setSystemMode] = createSignal<'light' | 'dark'>(getSystemMode())

  // Resolve actual mode (system -> light/dark)
  const resolvedMode = () => {
    const currentMode = mode()
    if (currentMode === 'system') {
      return systemMode()
    }
    return currentMode
  }

  // Listen for system preference changes
  onMount(() => {
    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)')
    const handler = (e: MediaQueryListEvent) => {
      setSystemMode(e.matches ? 'dark' : 'light')
    }
    mediaQuery.addEventListener('change', handler)
    return () => mediaQuery.removeEventListener('change', handler)
  })

  // Apply theme and mode to document
  createEffect(() => {
    const currentTheme = theme()
    const currentMode = resolvedMode()

    document.documentElement.dataset.theme = currentTheme
    document.documentElement.dataset.mode = currentMode

    // Update meta theme-color for mobile browsers
    const metaThemeColor = document.querySelector('meta[name="theme-color"]')
    if (metaThemeColor) {
      metaThemeColor.setAttribute('content', currentMode === 'dark' ? '#0a0a0b' : '#fafafa')
    }
  })

  // Persist to localStorage
  createEffect(() => {
    localStorage.setItem(THEME_STORAGE_KEY, theme())
  })

  createEffect(() => {
    localStorage.setItem(MODE_STORAGE_KEY, mode())
  })

  const setTheme = (newTheme: Theme) => {
    setThemeState(newTheme)
  }

  const setMode = (newMode: Mode) => {
    setModeState(newMode)
  }

  const toggleMode = () => {
    const current = mode()
    if (current === 'system') {
      // If system, switch to opposite of current system preference
      setModeState(systemMode() === 'dark' ? 'light' : 'dark')
    } else if (current === 'light') {
      setModeState('dark')
    } else {
      setModeState('light')
    }
  }

  const value: ThemeContextValue = {
    theme,
    mode,
    resolvedMode,
    setTheme,
    setMode,
    toggleMode,
  }

  return <ThemeContext.Provider value={value}>{props.children}</ThemeContext.Provider>
}

export function useTheme(): ThemeContextValue {
  const context = useContext(ThemeContext)
  if (!context) {
    throw new Error('useTheme must be used within a ThemeProvider')
  }
  return context
}
