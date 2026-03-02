/**
 * Review Modal
 *
 * Modal dialog for submitting a plugin review with a 1-5 star rating selector
 * and a text comment area. Used from the plugin detail view.
 */

import { Star, X } from 'lucide-solid'
import { type Component, createSignal, For } from 'solid-js'

interface ReviewModalProps {
  pluginId: string
  onSubmit: (rating: number, comment: string) => void
  onClose: () => void
}

export const ReviewModal: Component<ReviewModalProps> = (props) => {
  const [rating, setRating] = createSignal(0)
  const [hoveredStar, setHoveredStar] = createSignal(0)
  const [comment, setComment] = createSignal('')

  const displayRating = () => hoveredStar() || rating()

  const handleSubmit = () => {
    const r = rating()
    if (r < 1 || r > 5) return
    props.onSubmit(r, comment().trim())
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Escape') props.onClose()
  }

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label="Write a review"
      class="fixed inset-0 z-50 flex items-center justify-center bg-black/60"
      onKeyDown={handleKeyDown}
    >
      <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4">
        {/* Header */}
        <div class="flex items-center justify-between">
          <h3 class="text-sm font-semibold text-[var(--text-primary)]">Write a Review</h3>
          <button
            type="button"
            onClick={props.onClose}
            class="p-1 text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors rounded-[var(--radius-sm)]"
            aria-label="Close review modal"
          >
            <X class="w-4 h-4" />
          </button>
        </div>

        {/* Star rating selector */}
        <div class="space-y-1.5">
          <span class="text-[11px] text-[var(--text-secondary)]">Rating</span>
          <div
            role="radiogroup"
            aria-label="Star rating"
            class="flex items-center gap-1"
            onMouseLeave={() => setHoveredStar(0)}
          >
            <For each={[1, 2, 3, 4, 5]}>
              {(star) => (
                <button
                  type="button"
                  onClick={() => setRating(star)}
                  onMouseEnter={() => setHoveredStar(star)}
                  class="p-0.5 transition-transform hover:scale-110"
                  aria-label={`Rate ${star} star${star > 1 ? 's' : ''}`}
                >
                  <Star
                    class={`w-6 h-6 transition-colors ${
                      star <= displayRating()
                        ? 'text-[var(--warning)] fill-[var(--warning)]'
                        : 'text-[var(--text-muted)]'
                    }`}
                  />
                </button>
              )}
            </For>
          </div>
        </div>

        {/* Comment area */}
        <div class="space-y-1.5">
          <label class="text-[11px] text-[var(--text-secondary)]" for="review-comment">
            Comment
          </label>
          <textarea
            id="review-comment"
            value={comment()}
            onInput={(e) => setComment(e.currentTarget.value)}
            placeholder="Share your experience with this plugin..."
            rows={4}
            class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none resize-none"
          />
        </div>

        {/* Actions */}
        <div class="flex gap-2 justify-end">
          <button
            type="button"
            onClick={props.onClose}
            class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={handleSubmit}
            disabled={rating() === 0}
            class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-50"
          >
            Submit Review
          </button>
        </div>
      </div>
    </div>
  )
}
