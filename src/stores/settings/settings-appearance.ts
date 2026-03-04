/**
 * Settings Appearance
 * DOM appearance helpers: font maps, color utilities, and applyAppearance().
 * Reads the settings signal but does not own it (avoids circular deps).
 */

import type { AppSettings, BorderRadius, MonoFont, SansFont, UIDensity } from './settings-types'

// ============================================================================
// Font & Scale Lookup Tables
// ============================================================================

export const MONO_FONTS: Record<MonoFont, string> = {
  default: '"Geist Mono", "JetBrains Mono", "Fira Code", "SF Mono", monospace',
  jetbrains: '"JetBrains Mono", "Fira Code", "SF Mono", monospace',
  fira: '"Fira Code", "JetBrains Mono", "SF Mono", monospace',
}

export const SANS_FONTS: Record<SansFont, string> = {
  default: '"Geist", "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
  inter: '"Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
  outfit: '"Outfit", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
  nunito: '"Nunito Sans", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
}

export const RADIUS_SCALES: Record<BorderRadius, Record<string, string>> = {
  sharp: {
    '--radius-sm': '0px',
    '--radius-md': '2px',
    '--radius-lg': '3px',
    '--radius-xl': '4px',
    '--radius-2xl': '6px',
  },
  default: {
    '--radius-sm': '3px',
    '--radius-md': '5px',
    '--radius-lg': '6px',
    '--radius-xl': '10px',
    '--radius-2xl': '14px',
  },
  rounded: {
    '--radius-sm': '6px',
    '--radius-md': '8px',
    '--radius-lg': '10px',
    '--radius-xl': '14px',
    '--radius-2xl': '20px',
  },
  pill: {
    '--radius-sm': '10px',
    '--radius-md': '14px',
    '--radius-lg': '18px',
    '--radius-xl': '24px',
    '--radius-2xl': '9999px',
  },
}

export const DENSITY_SCALES: Record<UIDensity, Record<string, string>> = {
  compact: {
    '--density-spacing': '0.75',
    '--density-py': '0.25rem',
    '--density-px': '0.5rem',
    '--density-gap': '0.25rem',
    '--density-section-py': '0.375rem',
    '--density-section-px': '0.625rem',
  },
  default: {
    '--density-spacing': '1',
    '--density-py': '0.375rem',
    '--density-px': '0.75rem',
    '--density-gap': '0.5rem',
    '--density-section-py': '0.75rem',
    '--density-section-px': '1rem',
  },
  comfortable: {
    '--density-spacing': '1.25',
    '--density-py': '0.5rem',
    '--density-px': '1rem',
    '--density-gap': '0.75rem',
    '--density-section-py': '1rem',
    '--density-section-px': '1.25rem',
  },
}

// ============================================================================
// Color Utilities
// ============================================================================

/** Parse hex color (#rrggbb) to [r, g, b] */
function hexToRgb(hex: string): [number, number, number] {
  const h = hex.replace('#', '')
  return [
    Number.parseInt(h.substring(0, 2), 16),
    Number.parseInt(h.substring(2, 4), 16),
    Number.parseInt(h.substring(4, 6), 16),
  ]
}

/** Lighten/darken an [r,g,b] by a factor (>1 = lighter, <1 = darker) */
function adjustBrightness(rgb: [number, number, number], factor: number): [number, number, number] {
  return [
    Math.min(255, Math.round(rgb[0] * factor)),
    Math.min(255, Math.round(rgb[1] * factor)),
    Math.min(255, Math.round(rgb[2] * factor)),
  ]
}

/** Compute all accent CSS vars from a single hex color */
function hexToAccentVars(hex: string): Record<string, string> {
  const rgb = hexToRgb(hex)
  const lighter = adjustBrightness(rgb, 1.25)
  const darker = adjustBrightness(rgb, 0.78)
  const darkMuted = adjustBrightness(rgb, 0.35)
  return {
    '--accent': hex,
    '--accent-hover': `rgb(${lighter.join(',')})`,
    '--accent-active': `rgb(${darker.join(',')})`,
    '--accent-subtle': `rgba(${rgb.join(',')}, 0.15)`,
    '--accent-muted': `rgb(${darkMuted.join(',')})`,
    '--accent-border': `rgba(${rgb.join(',')}, 0.3)`,
    '--accent-glow': `rgba(${rgb.join(',')}, 0.4)`,
  }
}

/** Accent CSS var names to clean up when switching from custom to preset */
const ACCENT_VAR_NAMES = [
  '--accent',
  '--accent-hover',
  '--accent-active',
  '--accent-subtle',
  '--accent-muted',
  '--accent-border',
  '--accent-glow',
]

// ============================================================================
// DOM Application
// ============================================================================

/** Resolve the effective data-mode value from settings */
export function resolveMode(s: AppSettings): string {
  if (s.mode === 'system') {
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
  }
  // 'light' stays as-is; dark variants come from darkStyle
  if (s.mode === 'dark') {
    return s.appearance.darkStyle // 'dark' | 'midnight' | 'charcoal'
  }
  return s.mode
}

/** Resolve whether we're currently in a dark-like mode (for UI conditionals) */
export function isDarkMode(s: AppSettings): boolean {
  if (s.mode === 'system') {
    return window.matchMedia('(prefers-color-scheme: dark)').matches
  }
  return s.mode === 'dark'
}

/** Apply appearance settings to <html> element.
 *  Accepts a snapshot of AppSettings to avoid coupling to the signal. */
export function applyAppearanceToDOM(s: AppSettings): void {
  const el = document.documentElement

  // Color mode — resolve system + dark variants
  const resolved = resolveMode(s)
  el.dataset.mode = resolved

  // Accent color
  if (s.appearance.accentColor === 'custom') {
    delete el.dataset.accent
    const vars = hexToAccentVars(s.appearance.customAccentColor)
    for (const [prop, val] of Object.entries(vars)) {
      el.style.setProperty(prop, val)
    }
  } else {
    for (const prop of ACCENT_VAR_NAMES) {
      el.style.removeProperty(prop)
    }
    if (s.appearance.accentColor === 'violet') {
      delete el.dataset.accent
    } else {
      el.dataset.accent = s.appearance.accentColor
    }
  }

  // UI scale — save scroll ratios, change font-size, force sync reflow, restore
  const scrollContainers = document.querySelectorAll('[style*="translateZ"]')
  const scrollState: { el: Element; ratio: number }[] = []
  for (const sc of scrollContainers) {
    if (sc.scrollHeight > sc.clientHeight) {
      scrollState.push({
        el: sc,
        ratio: sc.scrollTop / (sc.scrollHeight - sc.clientHeight),
      })
    }
  }
  el.style.fontSize = `${16 * s.appearance.uiScale}px`
  void el.offsetHeight
  for (const { el: sc, ratio } of scrollState) {
    sc.scrollTop = ratio * (sc.scrollHeight - sc.clientHeight)
  }

  // Sans font
  el.style.setProperty('--font-sans', SANS_FONTS[s.appearance.fontSans])

  // Mono font
  el.style.setProperty('--font-mono', MONO_FONTS[s.appearance.fontMono])
  el.style.setProperty('--font-ui-mono', MONO_FONTS[s.appearance.fontMono])

  // Font ligatures
  if (s.appearance.fontLigatures) {
    el.style.setProperty('font-variant-ligatures', 'normal')
    el.style.setProperty('font-feature-settings', '"liga" 1, "calt" 1')
  } else {
    el.style.setProperty('font-variant-ligatures', 'none')
    el.style.setProperty('font-feature-settings', '"liga" 0, "calt" 0')
  }

  // Chat font size (absolute px, independent of uiScale)
  el.style.setProperty('--chat-font-size', `${s.appearance.chatFontSize}px`)

  // Border radius
  const radii = RADIUS_SCALES[s.appearance.borderRadius]
  for (const [prop, val] of Object.entries(radii)) {
    el.style.setProperty(prop, val)
  }

  // UI density
  const density = DENSITY_SCALES[s.appearance.density]
  for (const [prop, val] of Object.entries(density)) {
    el.style.setProperty(prop, val)
  }

  // Code theme
  if (s.appearance.codeTheme === 'default') {
    delete el.dataset.codeTheme
  } else {
    el.dataset.codeTheme = s.appearance.codeTheme
  }

  // High contrast
  if (s.appearance.highContrast) {
    el.dataset.highContrast = ''
  } else {
    delete el.dataset.highContrast
  }

  // Reduce motion
  if (s.appearance.reduceMotion) {
    el.dataset.reduceMotion = ''
  } else {
    delete el.dataset.reduceMotion
  }
}
