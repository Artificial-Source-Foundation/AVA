/**
 * Theme Presets
 * Named theme configurations that apply accent color, dark style,
 * code theme, and border radius all at once.
 */

import type {
  AccentColor,
  BorderRadius,
  CodeTheme,
  DarkStyle,
} from '../stores/settings/settings-types'

export interface ThemePreset {
  name: string
  mode: 'dark' | 'light'
  accentColor: AccentColor
  customAccentColor?: string
  darkStyle?: DarkStyle
  codeTheme: CodeTheme
  borderRadius: BorderRadius
  /** Color swatch shown in the preset card (CSS color) */
  swatch: string
  /** Secondary color for the swatch gradient */
  swatchAlt: string
}

export const THEME_PRESETS: ThemePreset[] = [
  {
    name: 'Catppuccin Mocha',
    mode: 'dark',
    accentColor: 'custom',
    customAccentColor: '#cba6f7',
    darkStyle: 'midnight',
    codeTheme: 'catppuccin',
    borderRadius: 'rounded',
    swatch: '#cba6f7',
    swatchAlt: '#1e1e2e',
  },
  {
    name: 'Catppuccin Latte',
    mode: 'light',
    accentColor: 'custom',
    customAccentColor: '#8839ef',
    codeTheme: 'catppuccin',
    borderRadius: 'rounded',
    swatch: '#8839ef',
    swatchAlt: '#eff1f5',
  },
  {
    name: 'Dracula',
    mode: 'dark',
    accentColor: 'custom',
    customAccentColor: '#bd93f9',
    darkStyle: 'midnight',
    codeTheme: 'monokai',
    borderRadius: 'default',
    swatch: '#bd93f9',
    swatchAlt: '#282a36',
  },
  {
    name: 'Nord',
    mode: 'dark',
    accentColor: 'custom',
    customAccentColor: '#88c0d0',
    darkStyle: 'charcoal',
    codeTheme: 'nord',
    borderRadius: 'default',
    swatch: '#88c0d0',
    swatchAlt: '#2e3440',
  },
  {
    name: 'Gruvbox Dark',
    mode: 'dark',
    accentColor: 'custom',
    customAccentColor: '#fabd2f',
    darkStyle: 'dark',
    codeTheme: 'monokai',
    borderRadius: 'default',
    swatch: '#fabd2f',
    swatchAlt: '#282828',
  },
  {
    name: 'Gruvbox Light',
    mode: 'light',
    accentColor: 'custom',
    customAccentColor: '#d65d0e',
    codeTheme: 'default',
    borderRadius: 'default',
    swatch: '#d65d0e',
    swatchAlt: '#fbf1c7',
  },
  {
    name: 'Solarized Dark',
    mode: 'dark',
    accentColor: 'custom',
    customAccentColor: '#268bd2',
    darkStyle: 'charcoal',
    codeTheme: 'solarized-dark',
    borderRadius: 'default',
    swatch: '#268bd2',
    swatchAlt: '#002b36',
  },
  {
    name: 'Solarized Light',
    mode: 'light',
    accentColor: 'custom',
    customAccentColor: '#268bd2',
    codeTheme: 'solarized-dark',
    borderRadius: 'default',
    swatch: '#268bd2',
    swatchAlt: '#fdf6e3',
  },
  {
    name: 'Tokyo Night',
    mode: 'dark',
    accentColor: 'custom',
    customAccentColor: '#7aa2f7',
    darkStyle: 'midnight',
    codeTheme: 'default',
    borderRadius: 'rounded',
    swatch: '#7aa2f7',
    swatchAlt: '#1a1b26',
  },
  {
    name: 'Rose Pine',
    mode: 'dark',
    accentColor: 'rose',
    darkStyle: 'midnight',
    codeTheme: 'default',
    borderRadius: 'rounded',
    swatch: '#ebbcba',
    swatchAlt: '#191724',
  },
  {
    name: 'One Dark',
    mode: 'dark',
    accentColor: 'custom',
    customAccentColor: '#61afef',
    darkStyle: 'dark',
    codeTheme: 'default',
    borderRadius: 'default',
    swatch: '#61afef',
    swatchAlt: '#282c34',
  },
  {
    name: 'GitHub Dark',
    mode: 'dark',
    accentColor: 'blue',
    darkStyle: 'dark',
    codeTheme: 'github-dark',
    borderRadius: 'default',
    swatch: '#58a6ff',
    swatchAlt: '#0d1117',
  },
  {
    name: 'Moonlight',
    mode: 'dark',
    accentColor: 'custom',
    customAccentColor: '#82aaff',
    darkStyle: 'midnight',
    codeTheme: 'default',
    borderRadius: 'pill',
    swatch: '#82aaff',
    swatchAlt: '#222436',
  },
  {
    name: 'Everforest',
    mode: 'dark',
    accentColor: 'green',
    darkStyle: 'charcoal',
    codeTheme: 'default',
    borderRadius: 'rounded',
    swatch: '#a7c080',
    swatchAlt: '#2d353b',
  },
]
