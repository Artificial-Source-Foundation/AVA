/**
 * Plugin review store -- local rating and review system for plugins.
 */

export interface PluginReview {
  id: string
  pluginId: string
  userId: string
  rating: number // 1-5
  comment: string
  createdAt: number
}

export type PluginReviewInput = Omit<PluginReview, 'id' | 'createdAt'>

let idCounter = 0

function generateId(): string {
  idCounter++
  return `review-${Date.now()}-${idCounter}`
}

export class ReviewStore {
  private reviews = new Map<string, PluginReview[]>()

  /**
   * Submit a new review for a plugin.
   * Validates rating is between 1-5 inclusive.
   * A user can only have one review per plugin -- subsequent submissions replace the old one.
   */
  submitReview(input: PluginReviewInput): PluginReview {
    if (!Number.isInteger(input.rating) || input.rating < 1 || input.rating > 5) {
      throw new Error(`Invalid rating: ${input.rating}. Must be an integer between 1 and 5.`)
    }

    if (!input.pluginId.trim()) {
      throw new Error('pluginId is required')
    }

    if (!input.userId.trim()) {
      throw new Error('userId is required')
    }

    const existing = this.reviews.get(input.pluginId) ?? []

    // Remove any previous review by the same user for this plugin
    const filtered = existing.filter((r) => r.userId !== input.userId)

    const review: PluginReview = {
      id: generateId(),
      pluginId: input.pluginId,
      userId: input.userId,
      rating: input.rating,
      comment: input.comment,
      createdAt: Date.now(),
    }

    filtered.push(review)
    this.reviews.set(input.pluginId, filtered)

    return review
  }

  /**
   * Get all reviews for a plugin, sorted by createdAt descending (newest first).
   */
  getReviews(pluginId: string): PluginReview[] {
    const reviews = this.reviews.get(pluginId) ?? []
    return [...reviews].sort((a, b) => {
      const timeDiff = b.createdAt - a.createdAt
      if (timeDiff !== 0) return timeDiff
      // Stable tiebreaker: higher counter ID was created later
      return b.id.localeCompare(a.id)
    })
  }

  /**
   * Get the average rating for a plugin, or null if no reviews exist.
   */
  getAverageRating(pluginId: string): number | null {
    const reviews = this.reviews.get(pluginId)
    if (!reviews || reviews.length === 0) {
      return null
    }

    const sum = reviews.reduce((acc, r) => acc + r.rating, 0)
    return sum / reviews.length
  }

  /**
   * Delete a review by its ID.
   * Returns true if the review was found and deleted, false otherwise.
   */
  deleteReview(reviewId: string): boolean {
    for (const [pluginId, reviews] of this.reviews) {
      const idx = reviews.findIndex((r) => r.id === reviewId)
      if (idx !== -1) {
        reviews.splice(idx, 1)
        if (reviews.length === 0) {
          this.reviews.delete(pluginId)
        }
        return true
      }
    }
    return false
  }

  /**
   * Get the total number of reviews for a plugin.
   */
  getReviewCount(pluginId: string): number {
    return (this.reviews.get(pluginId) ?? []).length
  }

  /**
   * Clear all reviews (useful for testing).
   */
  clear(): void {
    this.reviews.clear()
  }
}
