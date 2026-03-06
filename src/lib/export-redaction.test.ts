import { describe, expect, it } from 'vitest'
import { applyRedaction, type RedactionOptions } from './export-redaction'

// ============================================================================
// Helpers
// ============================================================================

const ALL_ON: RedactionOptions = { stripApiKeys: true, stripFilePaths: true, stripEmails: true }
const ALL_OFF: RedactionOptions = { stripApiKeys: false, stripFilePaths: false, stripEmails: false }

// ============================================================================
// applyRedaction – API keys
// ============================================================================

describe('applyRedaction – API keys', () => {
  const opts: RedactionOptions = { stripApiKeys: true, stripFilePaths: false, stripEmails: false }

  it('redacts OpenAI sk- keys', () => {
    const text = 'key is sk-abcdefghijklmnopqrst12345 ok'
    expect(applyRedaction(text, opts)).toBe('key is [REDACTED_KEY] ok')
  })

  it('redacts generic key- prefixed keys', () => {
    const text = 'use key-abcdefghijklmnopqrst12345'
    expect(applyRedaction(text, opts)).toBe('use [REDACTED_KEY]')
  })

  it('redacts Bearer tokens', () => {
    const text = 'Authorization: Bearer eyJhbGciOiJIUzI1NiIs.token.sig'
    expect(applyRedaction(text, opts)).toBe('Authorization: [REDACTED_KEY]')
  })

  it('redacts GitHub PATs (ghp_)', () => {
    const token = `ghp_${'A'.repeat(36)}`
    expect(applyRedaction(`token=${token}`, opts)).toBe('token=[REDACTED_KEY]')
  })

  it('redacts GitHub OAuth tokens (gho_)', () => {
    const token = `gho_${'B'.repeat(36)}`
    expect(applyRedaction(token, opts)).toBe('[REDACTED_KEY]')
  })

  it('redacts Slack bot tokens', () => {
    const text = 'xoxb-123456789-abcdef'
    expect(applyRedaction(text, opts)).toBe('[REDACTED_KEY]')
  })

  it('redacts Google API keys', () => {
    const key = `AIza${'X'.repeat(35)}`
    expect(applyRedaction(`gapi=${key}`, opts)).toBe('gapi=[REDACTED_KEY]')
  })

  it('redacts AWS access keys', () => {
    const key = `AKIA${'A'.repeat(16)}`
    expect(applyRedaction(`aws=${key}`, opts)).toBe('aws=[REDACTED_KEY]')
  })

  it('leaves short strings alone', () => {
    // 'sk-short' is under 20 chars so should not match
    expect(applyRedaction('sk-short', opts)).toBe('sk-short')
  })

  it('redacts multiple keys in same text', () => {
    const text = `sk-${'a'.repeat(30)} and key-${'b'.repeat(25)}`
    const result = applyRedaction(text, opts)
    expect(result).toBe('[REDACTED_KEY] and [REDACTED_KEY]')
  })

  it('does nothing when stripApiKeys is false', () => {
    const text = `sk-${'a'.repeat(30)}`
    expect(applyRedaction(text, ALL_OFF)).toBe(text)
  })
})

// ============================================================================
// applyRedaction – file paths
// ============================================================================

describe('applyRedaction – file paths', () => {
  const opts: RedactionOptions = { stripApiKeys: false, stripFilePaths: true, stripEmails: false }

  it('redacts Unix absolute paths under /home', () => {
    expect(applyRedaction('at /home/user/.config/file', opts)).toBe('at [REDACTED_PATH]')
  })

  it('redacts Unix paths under /Users', () => {
    expect(applyRedaction('open /Users/dev/project/src/main.ts', opts)).toBe('open [REDACTED_PATH]')
  })

  it('redacts paths under /tmp, /var, /etc, /opt, /usr, /root', () => {
    for (const dir of ['/tmp', '/var', '/etc', '/opt', '/usr', '/root']) {
      const text = `${dir}/some/path`
      expect(applyRedaction(text, opts)).toBe('[REDACTED_PATH]')
    }
  })

  it('redacts Windows absolute paths', () => {
    expect(applyRedaction('at C:\\Users\\dev\\project\\file.ts', opts)).toBe('at [REDACTED_PATH]')
  })

  it('leaves relative paths alone', () => {
    expect(applyRedaction('see ./src/main.ts', opts)).toBe('see ./src/main.ts')
  })

  it('does nothing when stripFilePaths is false', () => {
    const text = '/home/user/secret/file.txt'
    expect(applyRedaction(text, ALL_OFF)).toBe(text)
  })
})

// ============================================================================
// applyRedaction – emails
// ============================================================================

describe('applyRedaction – emails', () => {
  const opts: RedactionOptions = { stripApiKeys: false, stripFilePaths: false, stripEmails: true }

  it('redacts standard emails', () => {
    expect(applyRedaction('contact user@example.com please', opts)).toBe(
      'contact [REDACTED_EMAIL] please'
    )
  })

  it('redacts emails with dots and plus signs', () => {
    expect(applyRedaction('alice.b+tag@company.co.uk', opts)).toBe('[REDACTED_EMAIL]')
  })

  it('redacts multiple emails', () => {
    const text = 'from a@b.com to c@d.org'
    const result = applyRedaction(text, opts)
    expect(result).toBe('from [REDACTED_EMAIL] to [REDACTED_EMAIL]')
  })

  it('does nothing when stripEmails is false', () => {
    expect(applyRedaction('user@example.com', ALL_OFF)).toBe('user@example.com')
  })
})

// ============================================================================
// Combined / edge cases
// ============================================================================

describe('applyRedaction – combined', () => {
  it('redacts keys, paths, and emails in one pass', () => {
    const text = `token sk-${'x'.repeat(25)} at /home/user/app sent to dev@co.com`
    const result = applyRedaction(text, ALL_ON)
    expect(result).toContain('[REDACTED_KEY]')
    expect(result).toContain('[REDACTED_PATH]')
    expect(result).toContain('[REDACTED_EMAIL]')
    expect(result).not.toContain('sk-')
    expect(result).not.toContain('/home/')
    expect(result).not.toContain('dev@co.com')
  })

  it('returns empty string for empty input', () => {
    expect(applyRedaction('', ALL_ON)).toBe('')
  })

  it('returns original text when all options are off', () => {
    const text = `sk-${'z'.repeat(30)} /home/user/file user@test.com`
    expect(applyRedaction(text, ALL_OFF)).toBe(text)
  })

  it('handles text with no sensitive content', () => {
    const text = 'just a normal message with no secrets'
    expect(applyRedaction(text, ALL_ON)).toBe(text)
  })

  it('preserves surrounding whitespace and punctuation', () => {
    const text = '(user@example.com)'
    expect(
      applyRedaction(text, { stripApiKeys: false, stripFilePaths: false, stripEmails: true })
    ).toBe('([REDACTED_EMAIL])')
  })
})
