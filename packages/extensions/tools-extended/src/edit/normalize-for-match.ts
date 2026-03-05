const SMART_SINGLE_QUOTES = /[\u2018\u2019\u201A\u201B]/g
const SMART_DOUBLE_QUOTES = /[\u201C\u201D\u201E\u201F]/g
const UNICODE_DASHES = /[\u2010\u2011\u2012\u2013\u2014\u2015\u2212]/g
const UNICODE_SPACES = /[\u00A0\u2002-\u200A\u202F\u205F\u3000]/g

export function normalizeForMatch(input: string): string {
  return input
    .split('\n')
    .map((line) => line.trimEnd())
    .join('\n')
    .replace(SMART_SINGLE_QUOTES, "'")
    .replace(SMART_DOUBLE_QUOTES, '"')
    .replace(UNICODE_DASHES, '-')
    .replace(UNICODE_SPACES, ' ')
}
