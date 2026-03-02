/**
 * Session slug generation — human-readable identifiers from goal text.
 */

const STOP_WORDS = new Set([
  'a',
  'an',
  'the',
  'and',
  'or',
  'but',
  'in',
  'on',
  'at',
  'to',
  'for',
  'of',
  'with',
  'by',
  'is',
  'it',
  'as',
  'be',
  'do',
  'no',
  'so',
  'if',
  'my',
  'me',
  'we',
  'up',
  'am',
  'are',
  'was',
  'has',
  'had',
  'not',
  'this',
  'that',
  'from',
  'they',
  'been',
  'have',
  'its',
  'will',
  'can',
  'each',
  'which',
  'their',
  'then',
  'them',
  'into',
  'some',
])

const MAX_SLUG_LENGTH = 50
const MAX_WORDS = 6

/**
 * Generate a URL-friendly slug from a goal string.
 *
 * Takes the first few significant words, lowercases them, joins with hyphens,
 * strips non-alphanumeric characters (except hyphens), and limits to 50 chars.
 */
export function generateSlug(goal: string): string {
  if (!goal || typeof goal !== 'string') {
    return ''
  }

  // Lowercase and strip non-alphanumeric (keep spaces and hyphens for splitting)
  const cleaned = goal
    .toLowerCase()
    .replace(/[^a-z0-9\s-]/g, '')
    .trim()

  if (!cleaned) {
    return ''
  }

  // Split into words, filter stop words, take first N significant words
  const words = cleaned
    .split(/[\s-]+/)
    .filter((w) => w.length > 0)
    .filter((w) => !STOP_WORDS.has(w))

  // If all words were stop words, fall back to first few raw words
  const selectedWords =
    words.length > 0
      ? words.slice(0, MAX_WORDS)
      : cleaned
          .split(/[\s-]+/)
          .filter((w) => w.length > 0)
          .slice(0, MAX_WORDS)

  if (selectedWords.length === 0) {
    return ''
  }

  const slug = selectedWords.join('-')

  // Truncate to max length, but avoid cutting mid-word
  if (slug.length <= MAX_SLUG_LENGTH) {
    return slug
  }

  const truncated = slug.slice(0, MAX_SLUG_LENGTH)
  const lastHyphen = truncated.lastIndexOf('-')
  if (lastHyphen > 0) {
    return truncated.slice(0, lastHyphen)
  }
  return truncated
}
