# Estela Design System 2026

> Modern, minimal, dark-first design inspired by Cursor, Windsurf, Zed, and 2026 UI trends.

---

## Design Philosophy

### Core Principles

1. **Dark-First** - Dark mode is the primary experience, not an afterthought
2. **Minimal Chrome** - Reduce visual clutter, let content breathe
3. **Depth Through Glass** - Use glassmorphism for layering, not borders
4. **Subtle Motion** - Micro-interactions that feel natural, not flashy
5. **Developer-Focused** - Optimized for productivity, not decoration

### Inspirations

- **[Cursor 2.0](https://cursor.com)** - Agent-centric, multiple parallel workflows
- **[Windsurf](https://windsurf.com)** - Clean, Apple-like refinement
- **[Zed](https://zed.dev)** - Native performance feel, minimal UI
- **[Linear](https://linear.app)** - Smooth animations, keyboard-first
- **[Vercel](https://vercel.com)** - Glassmorphism, subtle gradients

---

## Color System

### Background Hierarchy

```css
/* True black base for OLED optimization */
--bg-base: #0a0a0b;        /* App background */
--bg-subtle: #111113;      /* Sidebar, panels */
--bg-raised: #18181b;      /* Cards, dropdowns */
--bg-overlay: #1f1f23;     /* Modals, popovers */
--bg-elevated: #27272a;    /* Hover states */
```

### Text Hierarchy

```css
/* Off-white for reduced eye strain */
--text-primary: #fafafa;   /* Primary content */
--text-secondary: #a1a1aa; /* Secondary, descriptions */
--text-tertiary: #71717a;  /* Hints, placeholders */
--text-muted: #52525b;     /* Disabled, subtle */
```

### Accent Colors (Neon-inspired)

```css
/* Primary accent - Electric violet */
--accent: #8b5cf6;
--accent-hover: #a78bfa;
--accent-subtle: rgba(139, 92, 246, 0.15);
--accent-glow: rgba(139, 92, 246, 0.4);

/* Status colors - Vibrant but not harsh */
--success: #22c55e;
--success-subtle: rgba(34, 197, 94, 0.15);

--warning: #eab308;
--warning-subtle: rgba(234, 179, 8, 0.15);

--error: #ef4444;
--error-subtle: rgba(239, 68, 68, 0.15);

--info: #3b82f6;
--info-subtle: rgba(59, 130, 246, 0.15);
```

### Provider-Specific Colors

```css
/* Each provider has a unique identity */
--provider-anthropic: #d97706;  /* Amber/orange */
--provider-openai: #10b981;     /* Emerald */
--provider-openrouter: #8b5cf6; /* Violet */
--provider-ollama: #3b82f6;     /* Blue */
--provider-google: #ea4335;     /* Red */
```

---

## Glassmorphism System

### Glass Layers (Implemented)

```css
/* Standard glass - cards, panels, toasts */
.glass {
  background: var(--glass-bg);           /* rgba(17, 17, 19, 0.65) */
  backdrop-filter: blur(var(--blur-md)); /* 16px */
  border: 1px solid var(--glass-border); /* rgba(255, 255, 255, 0.06) */
}

/* Strong glass - sidebar, dialogs */
.glass-strong {
  background: var(--glass-bg-strong);    /* rgba(24, 24, 27, 0.85) */
  backdrop-filter: blur(var(--blur-lg)); /* 24px */
  border: 1px solid var(--glass-border);
}
```

### Ambient Gradient Mesh (Implemented)

Glass surfaces need colorful backgrounds to look alive. The app uses a fixed gradient mesh behind all content:

```css
#root::before {
  background:
    radial-gradient(ellipse at 15% 80%, rgba(139, 92, 246, 0.08) 0%, transparent 50%),
    radial-gradient(ellipse at 85% 20%, rgba(59, 130, 246, 0.06) 0%, transparent 50%),
    radial-gradient(ellipse at 50% 50%, rgba(236, 72, 153, 0.04) 0%, transparent 50%),
    var(--background);
}
```

This creates subtle violet/blue/pink light leaks that give glass panels color variation when blurred.

### WebKitGTK Fallback

Tauri on Linux uses WebKitGTK which may not support `backdrop-filter`. An `@supports` fallback provides an opaque background:

```css
@supports not (backdrop-filter: blur(1px)) {
  .glass, .glass-strong {
    background: var(--glass-bg-strong); /* opaque fallback */
  }
}
```

### Gradient Overlays

```css
/* Subtle gradient for depth */
.gradient-subtle {
  background: linear-gradient(
    180deg,
    rgba(255, 255, 255, 0.02) 0%,
    rgba(255, 255, 255, 0) 100%
  );
}

/* Accent gradient for highlights */
.gradient-accent {
  background: linear-gradient(
    135deg,
    rgba(139, 92, 246, 0.15) 0%,
    rgba(139, 92, 246, 0.05) 100%
  );
}
```

---

## Typography

### Font Stack

```css
/* UI text - Geist Sans or Inter */
--font-sans: "Geist Sans", "Inter", -apple-system, BlinkMacSystemFont, sans-serif;

/* Code - Geist Mono or JetBrains Mono */
--font-mono: "Geist Mono", "JetBrains Mono", "Fira Code", monospace;
```

### Type Scale

| Name | Size | Weight | Use Case |
|------|------|--------|----------|
| `xs` | 11px | 400 | Badges, hints |
| `sm` | 13px | 400 | Secondary text |
| `base` | 14px | 400 | Body text |
| `md` | 15px | 500 | Emphasized text |
| `lg` | 16px | 600 | Section headers |
| `xl` | 18px | 600 | Page titles |

### Line Heights

- **Tight**: 1.25 (headings)
- **Normal**: 1.5 (body text)
- **Relaxed**: 1.75 (long-form content)

---

## Spacing & Layout

### Spacing Scale

```css
--space-0: 0;
--space-1: 4px;
--space-2: 8px;
--space-3: 12px;
--space-4: 16px;
--space-5: 20px;
--space-6: 24px;
--space-8: 32px;
--space-10: 40px;
--space-12: 48px;
```

### Border Radius

```css
--radius-sm: 4px;   /* Badges, small elements */
--radius-md: 6px;   /* Buttons, inputs */
--radius-lg: 8px;   /* Cards */
--radius-xl: 12px;  /* Large cards, modals */
--radius-2xl: 16px; /* Hero sections */
--radius-full: 9999px; /* Pills, avatars */
```

### Bento Grid System

For dashboard-style layouts, use asymmetric grids:

```css
.bento-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}

/* Feature card spans 2 columns */
.bento-feature {
  grid-column: span 2;
}

/* Hero card spans full width */
.bento-hero {
  grid-column: span 4;
}
```

---

## Components

### Cards

```
┌─────────────────────────────────────┐
│ ○ ○ ○  Card Title                   │  ← Subtle header
├─────────────────────────────────────┤
│                                     │
│  Content area with comfortable      │
│  padding and readable text          │
│                                     │
└─────────────────────────────────────┘
   ↑ 1px border with rgba(255,255,255,0.05)
```

**Styles:**
- Border: `1px solid rgba(255, 255, 255, 0.05)`
- Background: `rgba(24, 24, 27, 0.6)` with backdrop-blur
- Radius: `12px`
- Padding: `16px`
- No shadow (depth through glass, not shadow)

### Buttons

**Primary**
```css
.btn-primary {
  background: var(--accent);
  color: white;
  padding: 8px 16px;
  border-radius: 8px;
  font-weight: 500;
  transition: all 150ms ease;
}
.btn-primary:hover {
  background: var(--accent-hover);
  transform: translateY(-1px);
}
```

**Ghost**
```css
.btn-ghost {
  background: transparent;
  color: var(--text-secondary);
  padding: 8px 16px;
  border-radius: 8px;
}
.btn-ghost:hover {
  background: rgba(255, 255, 255, 0.05);
  color: var(--text-primary);
}
```

### Inputs

```css
.input {
  background: var(--bg-subtle);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 8px;
  padding: 10px 12px;
  color: var(--text-primary);
  transition: all 150ms ease;
}
.input:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 3px var(--accent-subtle);
  outline: none;
}
```

### Toggle Switch

```css
.toggle {
  width: 36px;
  height: 20px;
  border-radius: 10px;
  background: var(--bg-elevated);
  transition: background 200ms ease;
}
.toggle[data-checked="true"] {
  background: var(--accent);
}
.toggle-thumb {
  width: 16px;
  height: 16px;
  border-radius: 8px;
  background: white;
  transition: transform 200ms ease;
}
```

### Status Indicators

Use colored dots, not text badges:

```
● Connected (green)
● Disconnected (gray)
● Error (red)
● Loading (pulsing)
```

```css
.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--status-color);
}
.status-dot.connected { background: var(--success); }
.status-dot.error { background: var(--error); }
.status-dot.loading { animation: pulse 1.5s infinite; }
```

---

## Motion & Animation

### Timing Functions

```css
--ease-out: cubic-bezier(0.16, 1, 0.3, 1);      /* Smooth deceleration */
--ease-in-out: cubic-bezier(0.65, 0, 0.35, 1);  /* Balanced */
--ease-spring: cubic-bezier(0.34, 1.56, 0.64, 1); /* Bouncy overshoot */
```

### Duration Scale

```css
--duration-instant: 50ms;   /* Micro-interactions */
--duration-fast: 150ms;     /* Hovers, toggles */
--duration-normal: 200ms;   /* Standard transitions */
--duration-slow: 300ms;     /* Page transitions */
--duration-slower: 500ms;   /* Complex animations */
```

### Spring Physics (solid-motionone)

Components use `solid-motionone` for physics-based animations:

```tsx
import { Motion } from 'solid-motionone'
import { springs } from '../lib/motion'

// Button press spring
<Motion.div press={{ scale: 0.97 }} transition={{ duration: 0.15 }}>
  <Button>Click</Button>
</Motion.div>

// Chat bubble slide-up on mount
<Motion.div
  initial={{ opacity: 0, y: 8 }}
  animate={{ opacity: 1, y: 0 }}
  transition={{ duration: 0.25, easing: [0.34, 1.56, 0.64, 1] }}
/>

// Toast slide-in from right
<Motion.div
  initial={{ opacity: 0, x: '100%' }}
  animate={{ opacity: 1, x: 0 }}
  exit={{ opacity: 0, x: '100%' }}
/>
```

Spring presets defined in `src/lib/motion.ts`:

```typescript
export const springs = {
  gentle: { easing: 'spring(1, 120, 14, 0)' },
  snappy: { easing: 'spring(1, 300, 20, 0)' },
  bouncy: { easing: 'spring(1, 200, 10, 0)' },
}
```

### Reduced Motion

All animations respect `prefers-reduced-motion`:

```typescript
import { useReducedMotion } from '../lib/motion'

const reduced = useReducedMotion()
// Use to disable/simplify animations
```

### CSS Animation Classes

```css
.animate-pulse-subtle  /* Subtle badge/indicator pulse */
.animate-dropdown-in   /* Select dropdown with spring easing */
.animate-spin          /* Loading spinner */
.animate-bounce        /* Bouncing dots (typing indicator) */
.focus-glow            /* Input focus: ring + luminous shadow */
```

---

## Layout Patterns

### Resizable Sidebar + Main Content

AppShell uses `@corvu/resizable` for drag-to-resize panels:

```
┌──────────┬─┬──────────────────────────────────┐
│          │ │ TabBar [Chat][Agents][Files]...   │
│ Sidebar  │◀│ ─────────────────────────────────│
│ (22%)    │▶│ Active Panel Content              │
│ glass-   │ │                                   │
│ strong   │ │                                   │
│          │ ├──────────────────────────────────│
│          │ │ StatusBar                         │
└──────────┴─┴──────────────────────────────────┘
          ↑ Resize Handle (4px, accent on drag)
```

- Sidebar: min 12%, max 35%, collapsible to 4% (icon-only mode)
- Panel sizes persisted in localStorage
- Collapse/expand via drag past minimum or callbacks

### Three-Panel Layout

```
┌──────────┬─────────────────────┬────────────┐
│          │                     │            │
│ Sidebar  │      Primary        │   Detail   │
│  (240px) │      (flex-1)       │   (320px)  │
│          │                     │            │
└──────────┴─────────────────────┴────────────┘
```

### Settings Page

```
┌──────────────────────────────────────────────┐
│  ←  Settings                                 │
├──────────┬───────────────────────────────────┤
│          │                                   │
│  Nav     │         Content                   │
│  tabs    │         area                      │
│          │                                   │
└──────────┴───────────────────────────────────┘
```

---

## Iconography

### Icon Guidelines

- **Size**: 16px for inline, 20px for buttons, 24px for features
- **Stroke**: 1.5px for consistency with Lucide
- **Color**: Inherit from text color, use accent for emphasis
- **Style**: Outlined preferred, filled for active states

### Provider Icons

Each provider gets a custom icon with branded color:
- Anthropic: Sparkles (amber)
- OpenAI: CPU (emerald)
- OpenRouter: Zap (violet)
- Ollama: Bot (blue)
- Google: Star (red)

---

## Dark Mode Specifics

### Contrast Ratios

- Primary text: 15.8:1 (WCAG AAA)
- Secondary text: 7.2:1 (WCAG AA)
- Tertiary text: 4.6:1 (WCAG AA for large text)

### Avoiding Eye Strain

1. **No pure black** - Use `#0a0a0b` instead of `#000000`
2. **No pure white** - Use `#fafafa` instead of `#ffffff`
3. **Subtle borders** - Use `rgba(255, 255, 255, 0.05)` not solid colors
4. **Reduced contrast for secondary elements**

---

## WebKitGTK Performance (Tauri/Linux)

### Known Issues & Fixes

1. **No fixed pseudo-element overlays** — `pointer-events: none` on `position: fixed` pseudo-elements with high z-index doesn't work in WebKitGTK. Removed the noise texture overlay (`#root::after`) that was blocking all clicks.

2. **GPU layer promotion for scroll** — WebKitGTK doesn't auto-promote scroll containers. Add `transform: translateZ(0)` to force GPU compositing on scroll-heavy containers (settings page).

3. **`transition-colors` not `transition-all`** — Transitioning all CSS properties causes jank during scroll. Use `transition-colors` when only color changes are needed.

4. **No hover transforms** — `hover:-translate-y` on buttons causes layout reflow. Use opacity or color changes instead.

5. **Width-based sidebar toggle** — `margin-left: -Xpx` causes content to bleed behind the ActivityBar. Use `width: 0` with `overflow: hidden` and `transition: width 120ms ease`.

### Glow Effects (Sparingly)

```css
/* Accent glow for focus states */
.glow-accent {
  box-shadow: 0 0 20px rgba(139, 92, 246, 0.3);
}

/* Error glow for validation */
.glow-error {
  box-shadow: 0 0 20px rgba(239, 68, 68, 0.3);
}
```

---

## Responsive Behavior

### Breakpoints

```css
--bp-sm: 640px;
--bp-md: 768px;
--bp-lg: 1024px;
--bp-xl: 1280px;
--bp-2xl: 1536px;
```

### Desktop-First

Estela is a desktop app, so we design desktop-first:

1. Full sidebar visible by default
2. Multi-panel layouts
3. Keyboard shortcuts prominent
4. Dense information display

---

## Accessibility

### Keyboard Navigation

- All interactive elements focusable
- Focus rings visible: `box-shadow: 0 0 0 2px var(--accent)`
- Logical tab order
- Escape closes modals

### Screen Reader Support

- Semantic HTML elements
- ARIA labels for icon-only buttons
- Live regions for dynamic content
- Proper heading hierarchy

### Reduced Motion

```css
@media (prefers-reduced-motion: reduce) {
  * {
    animation-duration: 0.01ms !important;
    transition-duration: 0.01ms !important;
  }
}
```

---

## Implementation Checklist

### Tokens & Styles

- [x] Add glass/depth/blur/shadow tokens to `tokens.css`
- [x] Add ambient gradient mesh to `index.css`
- [x] Add glass utility classes (`.glass`, `.glass-strong`)
- [x] Add `@supports` fallback for WebKitGTK
- [x] Add focus-glow, pulse, dropdown-in animations
- [x] Add resize handle styles
- [ ] Update font stack to Geist

### Components Updated

- [x] Button - spring press animation via solid-motionone
- [x] Input/Textarea - focus-glow shadow effect
- [x] Card - wired `glass` prop to real `backdrop-filter`
- [x] Toggle - spring easing on thumb transition
- [x] Badge - added `pulse` prop for active indicators
- [x] Dialog - glass-strong background
- [x] Toast - spring slide-in via Motion.div, glass background
- [x] Select - spring dropdown animation, glass dropdown, focus-glow
- [x] ChatBubble - slide-up animation on mount
- [ ] Settings - bento-style cards

### Layout Updated

- [x] Sidebar - glass-strong, accepts `isCollapsed` prop from parent
- [x] AppShell - corvu resizable panels with drag-to-resize
- [x] MainContent - TabBar integration, panel switching, welcome state
- [x] StatusBar - model badge, connection indicator, integrated into layout
- [x] TabBar - rendered in MainContent, added Code tab
- [x] CodeEditorPanel - CodeMirror 6 code viewer (new component)

---

## Resources

### Design Trends Research

- [2026 UX/UI Design Trends](https://medium.com/@tanmayvatsa1507/2026-ux-ui-design-trends-that-will-be-everywhere-0cb83b572319)
- [23 UI Design Trends in 2026](https://musemind.agency/blog/ui-design-trends)
- [UI trends 2026: top 10 trends](https://www.uxstudioteam.com/ux-blog/ui-trends-2019)
- [Dark Mode Best Practices](https://designindc.com/blog/dark-mode-web-design-seo-ux-trends-for-2025/)
- [Glassmorphism Guide](https://contra.com/p/PYkeMOc7-design-trends-2025-glassmorphism-neumorphism-and-styles-you-need-to-know)
- [Bento Grid Layouts](https://medium.com/@support_82111/from-bento-boxes-to-brutalism-decoding-the-top-ui-design-trends-for-2025-f524d0a49569)

### Typography

- [Best Free Fonts for UI 2026](https://www.untitledui.com/blog/best-free-fonts)
- [Geist Font by Vercel](https://vercel.com/font)
- [Top 10 Monospaced Fonts](https://www.typewolf.com/top-10-monospaced-fonts)

### Tools

- [Colorffy Dark Theme Generator](https://colorffy.com/dark-theme-generator)
- [50 Shades of Dark Mode Gray](https://blog.karenying.com/posts/50-shades-of-dark-mode-gray/)
