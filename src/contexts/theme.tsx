/**
 * Theme Context for Estela
 *
 * Currently locked to minimal dark theme.
 * Theme system preserved for future expansion.
 */

import {
  type Accessor,
  createContext,
  createEffect,
  createSignal,
  type ParentComponent,
  useContext,
} from 'solid-js'

export type Theme = 'minimal' | 'glass' | 'terminal' | 'soft'
export type Mode = 'dark' | 'light' | 'system'

interface ThemeContextValue {
  theme: Accessor<Theme>
  mode: Accessor<Mode>
  resolvedMode: Accessor<'dark' | 'light'>
  setTheme: (theme: Theme) => void
  setMode: (mode: Mode) => void
  toggleMode: () => void
}

const ThemeContext = createContext<ThemeContextValue>()

export const ThemeProvider: ParentComponent = (props) => {
  const [theme] = createSignal<Theme>('minimal')
  const [mode] = createSignal<Mode>('dark')

  // Apply theme and mode to document
  createEffect(() => {
    document.documentElement.dataset.theme = 'minimal'
    document.documentElement.dataset.mode = 'dark'
  })

  // No-op setters for now (single theme)
  const setTheme = (_theme: Theme) => {}
  const setMode = (_mode: Mode) => {}
  const toggleMode = () => {}

  const value: ThemeContextValue = {
    theme,
    mode,
    resolvedMode: () => 'dark',
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
