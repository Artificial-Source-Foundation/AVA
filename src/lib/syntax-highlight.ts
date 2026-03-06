/**
 * Lightweight Syntax Highlighter
 *
 * Regex-based highlighting for common languages.
 * Outputs <span class="syn-*"> that map to --syntax-* CSS variables.
 * No external deps — keeps bundle small.
 */

import { type LanguageRules, languages } from './syntax-languages'

// ============================================================================
// Helpers
// ============================================================================

function escapeHtml(str: string): string {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
}

function keywordPattern(words: string[]): RegExp {
  return new RegExp(`\\b(${words.join('|')})\\b`, 'g')
}

// ============================================================================
// Highlighter
// ============================================================================

/**
 * Highlight code using regex patterns mapped to --syntax-* CSS vars.
 * Returns HTML string with <span class="syn-*"> wrappers.
 */
export function highlightCode(code: string, lang: string): string {
  const language: LanguageRules | undefined = languages[lang.toLowerCase()]
  if (!language) return escapeHtml(code)

  // Phase 1: Escape HTML in original code
  let result = escapeHtml(code)

  // Phase 2: Build list of all replacements from patterns
  // We use a marker-based approach: find matches, replace with unique markers,
  // then swap markers for spans at the end. This prevents double-highlighting.
  const markers: Array<{ marker: string; replacement: string }> = []
  let markerIndex = 0

  // Apply keyword/type highlighting from word lists
  if (language.keywords) {
    const kw = keywordPattern(language.keywords)
    result = result.replace(kw, (match) => {
      const marker = `\x00KW${markerIndex++}\x00`
      markers.push({ marker, replacement: `<span class="syn-keyword">${match}</span>` })
      return marker
    })
  }

  if (language.types) {
    const tp = keywordPattern(language.types)
    result = result.replace(tp, (match) => {
      const marker = `\x00TP${markerIndex++}\x00`
      markers.push({ marker, replacement: `<span class="syn-type">${match}</span>` })
      return marker
    })
  }

  // Apply pattern-based highlighting
  for (const rule of language.patterns) {
    // Create a fresh regex each time (avoid lastIndex issues)
    const re = new RegExp(rule.pattern.source, rule.pattern.flags)
    result = result.replace(re, (match) => {
      // Don't highlight if already inside a marker
      if (match.includes('\x00')) return match
      const marker = `\x00PT${markerIndex++}\x00`
      markers.push({ marker, replacement: `<span class="${rule.className}">${match}</span>` })
      return marker
    })
  }

  // Phase 3: Replace markers with actual spans
  for (const { marker, replacement } of markers) {
    result = result.replace(marker, replacement)
  }

  return result
}
