/**
 * Unicode Normalization for Edit Tool
 *
 * LLMs often generate "visually similar" but different Unicode characters.
 * This module normalizes these to ASCII equivalents for matching.
 *
 * Categories handled:
 * - Quotes: smart quotes → straight quotes
 * - Dashes: em/en dashes, minus → hyphen
 * - Spaces: non-breaking spaces, thin spaces → regular space
 * - Apostrophes: curly apostrophe → straight apostrophe
 * - Ellipsis: horizontal ellipsis → three dots
 */

// ============================================================================
// Character Mappings
// ============================================================================

/**
 * Smart quotes → straight quotes
 */
const QUOTE_MAPPINGS: Record<string, string> = {
  // Double quotes
  '\u201C': '"', // " LEFT DOUBLE QUOTATION MARK
  '\u201D': '"', // " RIGHT DOUBLE QUOTATION MARK
  '\u201E': '"', // „ DOUBLE LOW-9 QUOTATION MARK
  '\u201F': '"', // ‟ DOUBLE HIGH-REVERSED-9 QUOTATION MARK
  '\u00AB': '"', // « LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
  '\u00BB': '"', // » RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
  '\u301D': '"', // 〝 REVERSED DOUBLE PRIME QUOTATION MARK
  '\u301E': '"', // 〞 DOUBLE PRIME QUOTATION MARK
  '\u301F': '"', // 〟 LOW DOUBLE PRIME QUOTATION MARK

  // Single quotes
  '\u2018': "'", // ' LEFT SINGLE QUOTATION MARK
  '\u2019': "'", // ' RIGHT SINGLE QUOTATION MARK
  '\u201A': "'", // ‚ SINGLE LOW-9 QUOTATION MARK
  '\u201B': "'", // ‛ SINGLE HIGH-REVERSED-9 QUOTATION MARK
  '\u2039': "'", // ‹ SINGLE LEFT-POINTING ANGLE QUOTATION MARK
  '\u203A': "'", // › SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
  '\u0060': "'", // ` GRAVE ACCENT (often used as quote)
  '\u00B4': "'", // ´ ACUTE ACCENT
}

/**
 * Dashes → hyphen-minus
 */
const DASH_MAPPINGS: Record<string, string> = {
  '\u2010': '-', // ‐ HYPHEN
  '\u2011': '-', // ‑ NON-BREAKING HYPHEN
  '\u2012': '-', // ‒ FIGURE DASH
  '\u2013': '-', // – EN DASH
  '\u2014': '-', // — EM DASH
  '\u2015': '-', // ― HORIZONTAL BAR
  '\u2212': '-', // − MINUS SIGN
  '\uFE58': '-', // ﹘ SMALL EM DASH
  '\uFE63': '-', // ﹣ SMALL HYPHEN-MINUS
  '\uFF0D': '-', // － FULLWIDTH HYPHEN-MINUS
}

/**
 * Spaces → regular space
 */
const SPACE_MAPPINGS: Record<string, string> = {
  '\u00A0': ' ', //   NO-BREAK SPACE
  '\u2000': ' ', //   EN QUAD
  '\u2001': ' ', //   EM QUAD
  '\u2002': ' ', //   EN SPACE
  '\u2003': ' ', //   EM SPACE
  '\u2004': ' ', //   THREE-PER-EM SPACE
  '\u2005': ' ', //   FOUR-PER-EM SPACE
  '\u2006': ' ', //   SIX-PER-EM SPACE
  '\u2007': ' ', //   FIGURE SPACE
  '\u2008': ' ', //   PUNCTUATION SPACE
  '\u2009': ' ', //   THIN SPACE
  '\u200A': ' ', //   HAIR SPACE
  '\u202F': ' ', //   NARROW NO-BREAK SPACE
  '\u205F': ' ', //   MEDIUM MATHEMATICAL SPACE
  '\u3000': ' ', // 　 IDEOGRAPHIC SPACE
  '\uFEFF': '', //   ZERO WIDTH NO-BREAK SPACE (BOM - remove entirely)
}

/**
 * Other punctuation normalizations
 */
const OTHER_MAPPINGS: Record<string, string> = {
  // Ellipsis
  '\u2026': '...', // … HORIZONTAL ELLIPSIS

  // Arrows (sometimes appear in code comments)
  '\u2192': '->', // → RIGHTWARDS ARROW
  '\u21D2': '=>', // ⇒ RIGHTWARDS DOUBLE ARROW
  '\u2190': '<-', // ← LEFTWARDS ARROW
  '\u21D0': '<=', // ⇐ LEFTWARDS DOUBLE ARROW

  // Multiplication/division
  '\u00D7': '*', // × MULTIPLICATION SIGN
  '\u00F7': '/', // ÷ DIVISION SIGN

  // Bullets (in comments)
  '\u2022': '*', // • BULLET
  '\u2023': '*', // ‣ TRIANGULAR BULLET
  '\u25E6': '*', // ◦ WHITE BULLET

  // Copyright/trademark (sometimes in code)
  '\u00A9': '(c)', // © COPYRIGHT SIGN
  '\u00AE': '(R)', // ® REGISTERED SIGN
  '\u2122': '(TM)', // ™ TRADE MARK SIGN
}

// ============================================================================
// Combined Mapping
// ============================================================================

/**
 * All character mappings combined
 */
export const UNICODE_MAPPINGS: Record<string, string> = {
  ...QUOTE_MAPPINGS,
  ...DASH_MAPPINGS,
  ...SPACE_MAPPINGS,
  ...OTHER_MAPPINGS,
}

/**
 * Regex pattern for all mapped characters
 */
const UNICODE_PATTERN = new RegExp(`[${Object.keys(UNICODE_MAPPINGS).join('')}]`, 'g')

// ============================================================================
// Normalization Functions
// ============================================================================

/**
 * Normalize Unicode characters to ASCII equivalents
 *
 * @param text - Text to normalize
 * @returns Normalized text
 */
export function normalizeUnicode(text: string): string {
  // First, apply NFC normalization (canonical decomposition + composition)
  // This handles combining characters and diacritics
  const nfc = text.normalize('NFC')

  // Then apply our custom mappings
  return nfc.replace(UNICODE_PATTERN, (char) => UNICODE_MAPPINGS[char] ?? char)
}

/**
 * Check if text contains any characters that would be normalized
 */
export function hasNormalizableChars(text: string): boolean {
  return UNICODE_PATTERN.test(text)
}

/**
 * Get details about what would be normalized
 */
export function getNormalizationDetails(text: string): Array<{
  char: string
  replacement: string
  position: number
  name: string
}> {
  const details: Array<{
    char: string
    replacement: string
    position: number
    name: string
  }> = []

  for (let i = 0; i < text.length; i++) {
    const char = text[i]
    if (char in UNICODE_MAPPINGS) {
      details.push({
        char,
        replacement: UNICODE_MAPPINGS[char],
        position: i,
        name: getCharName(char),
      })
    }
  }

  return details
}

/**
 * Get human-readable name for a character
 */
function getCharName(char: string): string {
  const names: Record<string, string> = {
    '\u201C': 'left double quote',
    '\u201D': 'right double quote',
    '\u2018': 'left single quote',
    '\u2019': 'right single quote',
    '\u2013': 'en dash',
    '\u2014': 'em dash',
    '\u00A0': 'non-breaking space',
    '\u2026': 'ellipsis',
    '\u2192': 'right arrow',
    '\u21D2': 'double right arrow',
  }
  return names[char] ?? `U+${char.charCodeAt(0).toString(16).toUpperCase().padStart(4, '0')}`
}

// ============================================================================
// Exports
// ============================================================================

export { QUOTE_MAPPINGS, DASH_MAPPINGS, SPACE_MAPPINGS, OTHER_MAPPINGS }
