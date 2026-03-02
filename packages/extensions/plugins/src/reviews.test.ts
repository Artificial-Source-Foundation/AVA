import { beforeEach, describe, expect, it } from 'vitest'
import { ReviewStore } from './reviews.js'

describe('ReviewStore', () => {
  let store: ReviewStore

  beforeEach(() => {
    store = new ReviewStore()
  })

  describe('submitReview', () => {
    it('creates a review with generated id and timestamp', () => {
      const review = store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 4,
        comment: 'Great plugin!',
      })

      expect(review.id).toMatch(/^review-/)
      expect(review.pluginId).toBe('plugin-a')
      expect(review.userId).toBe('user-1')
      expect(review.rating).toBe(4)
      expect(review.comment).toBe('Great plugin!')
      expect(review.createdAt).toBeGreaterThan(0)
    })

    it('rejects rating below 1', () => {
      expect(() =>
        store.submitReview({
          pluginId: 'plugin-a',
          userId: 'user-1',
          rating: 0,
          comment: '',
        })
      ).toThrow('Invalid rating: 0')
    })

    it('rejects rating above 5', () => {
      expect(() =>
        store.submitReview({
          pluginId: 'plugin-a',
          userId: 'user-1',
          rating: 6,
          comment: '',
        })
      ).toThrow('Invalid rating: 6')
    })

    it('rejects non-integer rating', () => {
      expect(() =>
        store.submitReview({
          pluginId: 'plugin-a',
          userId: 'user-1',
          rating: 3.5,
          comment: '',
        })
      ).toThrow('Invalid rating: 3.5')
    })

    it('rejects empty pluginId', () => {
      expect(() =>
        store.submitReview({
          pluginId: '',
          userId: 'user-1',
          rating: 3,
          comment: '',
        })
      ).toThrow('pluginId is required')
    })

    it('rejects empty userId', () => {
      expect(() =>
        store.submitReview({
          pluginId: 'plugin-a',
          userId: '  ',
          rating: 3,
          comment: '',
        })
      ).toThrow('userId is required')
    })

    it('replaces existing review by same user for same plugin', () => {
      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 3,
        comment: 'Okay',
      })

      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 5,
        comment: 'Actually amazing!',
      })

      const reviews = store.getReviews('plugin-a')
      expect(reviews).toHaveLength(1)
      expect(reviews[0].rating).toBe(5)
      expect(reviews[0].comment).toBe('Actually amazing!')
    })

    it('allows different users to review the same plugin', () => {
      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 4,
        comment: 'Good',
      })

      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-2',
        rating: 5,
        comment: 'Excellent',
      })

      expect(store.getReviews('plugin-a')).toHaveLength(2)
    })
  })

  describe('getReviews', () => {
    it('returns empty array for unknown plugin', () => {
      expect(store.getReviews('nonexistent')).toEqual([])
    })

    it('returns reviews sorted by createdAt descending', () => {
      const r1 = store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 3,
        comment: 'First',
      })

      const r2 = store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-2',
        rating: 4,
        comment: 'Second',
      })

      const reviews = store.getReviews('plugin-a')
      expect(reviews).toHaveLength(2)
      // Newest first
      expect(reviews[0].createdAt).toBeGreaterThanOrEqual(reviews[1].createdAt)
      // The second submission should be first (newer)
      expect(reviews[0].id).toBe(r2.id)
      expect(reviews[1].id).toBe(r1.id)
    })

    it('does not return reviews from other plugins', () => {
      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 5,
        comment: '',
      })

      store.submitReview({
        pluginId: 'plugin-b',
        userId: 'user-1',
        rating: 2,
        comment: '',
      })

      expect(store.getReviews('plugin-a')).toHaveLength(1)
      expect(store.getReviews('plugin-b')).toHaveLength(1)
    })
  })

  describe('getAverageRating', () => {
    it('returns null for plugin with no reviews', () => {
      expect(store.getAverageRating('nonexistent')).toBeNull()
    })

    it('returns the rating for a single review', () => {
      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 4,
        comment: '',
      })

      expect(store.getAverageRating('plugin-a')).toBe(4)
    })

    it('returns the average across multiple reviews', () => {
      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 3,
        comment: '',
      })

      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-2',
        rating: 5,
        comment: '',
      })

      expect(store.getAverageRating('plugin-a')).toBe(4)
    })

    it('handles fractional averages', () => {
      store.submitReview({ pluginId: 'p', userId: 'u1', rating: 1, comment: '' })
      store.submitReview({ pluginId: 'p', userId: 'u2', rating: 2, comment: '' })
      store.submitReview({ pluginId: 'p', userId: 'u3', rating: 3, comment: '' })

      expect(store.getAverageRating('p')).toBe(2)
    })
  })

  describe('deleteReview', () => {
    it('deletes an existing review and returns true', () => {
      const review = store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 4,
        comment: 'Nice',
      })

      expect(store.deleteReview(review.id)).toBe(true)
      expect(store.getReviews('plugin-a')).toHaveLength(0)
    })

    it('returns false for a nonexistent review id', () => {
      expect(store.deleteReview('does-not-exist')).toBe(false)
    })

    it('only deletes the targeted review', () => {
      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 3,
        comment: '',
      })

      const r2 = store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-2',
        rating: 5,
        comment: '',
      })

      store.deleteReview(r2.id)

      const remaining = store.getReviews('plugin-a')
      expect(remaining).toHaveLength(1)
      expect(remaining[0].userId).toBe('user-1')
    })

    it('updates average rating after deletion', () => {
      store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 2,
        comment: '',
      })

      const r2 = store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-2',
        rating: 4,
        comment: '',
      })

      expect(store.getAverageRating('plugin-a')).toBe(3)

      store.deleteReview(r2.id)

      expect(store.getAverageRating('plugin-a')).toBe(2)
    })

    it('returns null average after deleting all reviews', () => {
      const r = store.submitReview({
        pluginId: 'plugin-a',
        userId: 'user-1',
        rating: 5,
        comment: '',
      })

      store.deleteReview(r.id)

      expect(store.getAverageRating('plugin-a')).toBeNull()
    })
  })

  describe('getReviewCount', () => {
    it('returns 0 for unknown plugin', () => {
      expect(store.getReviewCount('nope')).toBe(0)
    })

    it('returns the correct count', () => {
      store.submitReview({ pluginId: 'p', userId: 'u1', rating: 3, comment: '' })
      store.submitReview({ pluginId: 'p', userId: 'u2', rating: 4, comment: '' })

      expect(store.getReviewCount('p')).toBe(2)
    })
  })

  describe('clear', () => {
    it('removes all reviews', () => {
      store.submitReview({ pluginId: 'p1', userId: 'u1', rating: 3, comment: '' })
      store.submitReview({ pluginId: 'p2', userId: 'u1', rating: 5, comment: '' })

      store.clear()

      expect(store.getReviews('p1')).toEqual([])
      expect(store.getReviews('p2')).toEqual([])
      expect(store.getReviewCount('p1')).toBe(0)
    })
  })
})
