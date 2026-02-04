# Design System

> Estela's theming and styling architecture

---

## Overview

Estela uses CSS custom properties (design tokens) for consistent styling across 4 themes with light/dark mode support.

---

## Themes

### Glass (Default)
Apple-inspired with subtle frosted glass effects.

- Rounded corners (`--radius-lg: 12px`)
- Blur backgrounds
- Soft shadows with elevation
- Premium, modern feel

### Minimal
Linear-inspired, sharp aesthetic.

- Smaller corner radius (`--radius-lg: 8px`)
- Crisp edges
- Minimal shadows
- Clean, professional

### Terminal
Developer-focused with Catppuccin colors.

- Dark-only theme
- Monospace headings
- Code-like aesthetics
- Tight spacing

### Soft
Warm, friendly aesthetic.

- Very rounded corners (`--radius-lg: 16px`)
- Warm color undertones
- Softer shadows
- Approachable feel

---

## Color Tokens

### Primary Colors

```css
--accent           /* Brand color (purple-500) */
--accent-hover     /* Hover state */
--accent-active    /* Pressed state */
--accent-subtle    /* Muted background */
```

### Surfaces

```css
--background       /* App background */
--surface          /* Card/panel surface */
--surface-raised   /* Elevated surfaces */
--surface-overlay  /* Modal backgrounds */
--surface-sunken   /* Secondary areas */
```

### Text

```css
--text-primary     /* Main text (high contrast) */
--text-secondary   /* Secondary text */
--text-tertiary    /* Tertiary text */
--text-muted       /* Lowest priority */
```

### Borders

```css
--border-subtle    /* Dividers, light separators */
--border-default   /* Standard borders */
--border-strong    /* Emphasized borders */
```

### Feedback

```css
--success          /* Green - positive actions */
--success-subtle   /* Green background */
--warning          /* Yellow/orange - caution */
--warning-subtle   /* Warning background */
--error            /* Red - errors, destructive */
--error-subtle     /* Error background */
--info             /* Blue - informational */
--info-subtle      /* Info background */
```

### Sidebar

```css
--sidebar-background   /* Sidebar bg */
--sidebar-border       /* Sidebar border */
--sidebar-item-hover   /* Item hover state */
```

---

## Spacing Scale

Based on 4px grid:

| Token | Value |
|-------|-------|
| `--space-1` | 4px |
| `--space-2` | 8px |
| `--space-3` | 12px |
| `--space-4` | 16px |
| `--space-5` | 20px |
| `--space-6` | 24px |
| `--space-8` | 32px |
| `--space-10` | 40px |
| `--space-12` | 48px |

---

## Border Radius

| Token | Minimal | Glass | Soft |
|-------|---------|-------|------|
| `--radius-sm` | 4px | 6px | 8px |
| `--radius-md` | 6px | 8px | 12px |
| `--radius-lg` | 8px | 12px | 16px |
| `--radius-xl` | 10px | 16px | 20px |
| `--radius-full` | 9999px | 9999px | 9999px |

---

## Typography

### Font Families

```css
--font-sans      /* System sans-serif stack */
--font-mono      /* Monospace for code */
--font-display   /* Headings (theme-specific) */
```

### Font Sizes

```css
--text-xs     /* 11px */
--text-sm     /* 13px */
--text-base   /* 14px */
--text-lg     /* 16px */
--text-xl     /* 18px */
--text-2xl    /* 20px */
```

---

## Shadows

```css
--shadow-xs    /* Subtle */
--shadow-sm    /* Small */
--shadow-md    /* Medium */
--shadow-lg    /* Large */
--shadow-xl    /* Extra large */
--shadow-2xl   /* Huge */
--shadow-glass /* Glassmorphism effect */
```

---

## Animation

### Durations

```css
--duration-fast     /* 150ms - Quick interactions */
--duration-normal   /* 200ms - Standard */
--duration-slow     /* 300ms - Deliberate */
--duration-slower   /* 500ms - Long-form */
```

### Easing Functions

```css
--ease-out       /* cubic-bezier(0, 0, 0.2, 1) */
--ease-in-out    /* cubic-bezier(0.4, 0, 0.2, 1) */
--ease-spring    /* cubic-bezier(0.16, 1, 0.3, 1) */
```

### Animation Classes

```css
.animate-fade-in    /* Opacity: 0 → 1 */
.animate-slide-up   /* Y: 8px ↓ + fade */
.animate-slide-down /* Y: -8px ↑ + fade */
.animate-scale-in   /* Scale: 0.95 → 1 + fade */
.animate-spin       /* Full rotation */
.animate-pulse      /* Opacity pulse */
.animate-shimmer    /* Loading shimmer */
```

---

## Z-Index Scale

```css
--z-dropdown    /* 50 - Dropdowns */
--z-sticky      /* 100 - Sticky headers */
--z-overlay     /* 200 - Overlays */
--z-modal       /* 300 - Modals */
--z-popover     /* 400 - Popovers */
--z-tooltip     /* 500 - Tooltips */
```

---

## Usage Examples

### Using Tokens in Components

```tsx
// Always use CSS variables
<div class="bg-[var(--surface)] text-[var(--text-primary)]">
  <button class="
    px-4 py-2
    bg-[var(--accent)]
    hover:bg-[var(--accent-hover)]
    rounded-[var(--radius-lg)]
    transition-colors duration-[var(--duration-fast)]
  ">
    Click me
  </button>
</div>
```

### Conditional Styling

```tsx
<div class={`
  rounded-[var(--radius-md)]
  ${isActive()
    ? 'bg-[var(--accent-subtle)] border-[var(--accent)]'
    : 'bg-[var(--surface)] border-[var(--border-subtle)]'}
`}>
```

### Hover States

```tsx
<button class="
  text-[var(--text-secondary)]
  hover:text-[var(--text-primary)]
  hover:bg-[var(--surface-raised)]
  transition-colors duration-[var(--duration-fast)]
">
```

### Interactive Elements

```tsx
// Use the interactive utility class
<div class="interactive">
  Hover lift + shadow + color transitions
</div>

// Press animation
<button class="active:scale-[0.98]">
  Click me
</button>
```

---

## Theme Switching

Themes are applied via CSS classes on the root element:

```tsx
// In ThemeProvider
document.documentElement.classList.add(
  `theme-${themeName}`,
  isDark ? 'dark' : 'light'
)
```

### Applying Themes

```tsx
import { ThemeProvider, useTheme } from './contexts/theme'

function App() {
  const { theme, setTheme, mode, setMode } = useTheme()

  return (
    <button onClick={() => setTheme('glass')}>Glass Theme</button>
    <button onClick={() => setMode('dark')}>Dark Mode</button>
  )
}
```

---

## Best Practices

### Do

- Use design tokens for all colors, spacing, and radii
- Use `transition-colors duration-[var(--duration-fast)]` for hover states
- Use semantic color tokens (`--success`, `--error`) for feedback
- Test all 4 themes + light/dark modes

### Don't

- Hardcode colors (`#ffffff`, `rgb(...)`)
- Use arbitrary pixel values for spacing
- Skip hover/active states on interactive elements
- Forget dark mode variants

---

## Color Palette Reference

### Light Mode

```
Background: #fafafa (gray-50)
Surface: white
Text Primary: #18181b (gray-900)
Accent: #a855f7 (purple-500)
Border: #e4e4e7 (gray-200)
```

### Dark Mode

```
Background: #020617 (slate-950)
Surface: #0f172a (slate-900)
Text Primary: #e2e8f0 (slate-100)
Accent: #a855f7 (purple-500)
Border: rgba(255, 255, 255, 0.08)
```
