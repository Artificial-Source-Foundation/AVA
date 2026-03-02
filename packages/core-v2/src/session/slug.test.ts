import { describe, expect, it } from 'vitest'
import { generateSlug } from './slug.js'

describe('generateSlug', () => {
  it('generates a slug from a simple goal', () => {
    expect(generateSlug('Fix the login bug')).toBe('fix-login-bug')
  })

  it('lowercases all words', () => {
    expect(generateSlug('Create New Component')).toBe('create-new-component')
  })

  it('strips special characters', () => {
    expect(generateSlug("Fix user's profile! @page")).toBe('fix-users-profile-page')
  })

  it('filters out common stop words', () => {
    expect(generateSlug('Add a new feature to the app')).toBe('add-new-feature-app')
  })

  it('limits to max 50 characters', () => {
    const longGoal =
      'implement comprehensive error handling across all services and middleware layers in the backend'
    const slug = generateSlug(longGoal)
    expect(slug.length).toBeLessThanOrEqual(50)
    expect(slug.endsWith('-')).toBe(false)
  })

  it('returns empty string for empty input', () => {
    expect(generateSlug('')).toBe('')
  })

  it('returns empty string for undefined-like input', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    expect(generateSlug(undefined as unknown as string)).toBe('')
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    expect(generateSlug(null as unknown as string)).toBe('')
  })

  it('returns empty string for special chars only', () => {
    expect(generateSlug('!@#$%^&*()')).toBe('')
  })

  it('handles single word', () => {
    expect(generateSlug('refactor')).toBe('refactor')
  })

  it('collapses multiple spaces and hyphens', () => {
    expect(generateSlug('fix   the   broken   tests')).toBe('fix-broken-tests')
  })

  it('limits to 6 significant words', () => {
    const slug = generateSlug('update render logic cache state store handler fallback')
    const words = slug.split('-')
    expect(words.length).toBeLessThanOrEqual(6)
  })

  it('falls back to raw words when all are stop words', () => {
    const slug = generateSlug('is it a the')
    expect(slug.length).toBeGreaterThan(0)
  })

  it('truncates at word boundary', () => {
    const longGoal = 'implement authentication middleware validation pipeline across microservices'
    const slug = generateSlug(longGoal)
    expect(slug.length).toBeLessThanOrEqual(50)
    // Should not end with a partial word (no trailing hyphen)
    expect(slug.endsWith('-')).toBe(false)
  })
})
