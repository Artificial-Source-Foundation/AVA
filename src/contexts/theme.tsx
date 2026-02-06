/**
 * Theme Context for Estela
 *
 * Single dark theme - no switching needed.
 * Kept minimal for future expansion if needed.
 */

import { createContext, type ParentComponent, useContext } from 'solid-js'

interface ThemeContextValue {
  theme: 'dark'
}

const ThemeContext = createContext<ThemeContextValue>({ theme: 'dark' })

export const ThemeProvider: ParentComponent = (props) => {
  return <ThemeContext.Provider value={{ theme: 'dark' }}>{props.children}</ThemeContext.Provider>
}

export function useTheme(): ThemeContextValue {
  return useContext(ThemeContext)
}
