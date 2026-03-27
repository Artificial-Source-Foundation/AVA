import { type Component, lazy, Show, Suspense } from 'solid-js'
import { useHq } from '../../stores/hq'

const HqDashboard = lazy(() => import('./screens/HqDashboard'))
const HqDirectorChat = lazy(() => import('./screens/HqDirectorChat'))
const HqOrgChart = lazy(() => import('./screens/HqOrgChart'))
const HqPlanReview = lazy(() => import('./screens/HqPlanReview'))
const HqEpics = lazy(() => import('./screens/HqEpics'))
const HqEpicDetail = lazy(() => import('./screens/HqEpicDetail'))
const HqIssues = lazy(() => import('./screens/HqIssues'))
const HqIssueDetail = lazy(() => import('./screens/HqIssueDetail'))
const HqAgentDetail = lazy(() => import('./screens/HqAgentDetail'))

export const HqContent: Component = () => {
  const { hqPage } = useHq()

  return (
    <div class="flex-1 min-w-0 overflow-hidden">
      <Suspense
        fallback={
          <div
            class="flex items-center justify-center h-full"
            style={{ color: 'var(--text-muted)' }}
          >
            Loading...
          </div>
        }
      >
        <Show when={hqPage() === 'dashboard'}>
          <HqDashboard />
        </Show>
        <Show when={hqPage() === 'director-chat'}>
          <HqDirectorChat />
        </Show>
        <Show when={hqPage() === 'org-chart'}>
          <HqOrgChart />
        </Show>
        <Show when={hqPage() === 'plan-review'}>
          <HqPlanReview />
        </Show>
        <Show when={hqPage() === 'epics'}>
          <HqEpics />
        </Show>
        <Show when={hqPage() === 'epic-detail'}>
          <HqEpicDetail />
        </Show>
        <Show when={hqPage() === 'issues'}>
          <HqIssues />
        </Show>
        <Show when={hqPage() === 'issue-detail'}>
          <HqIssueDetail />
        </Show>
        <Show when={hqPage() === 'agent-detail'}>
          <HqAgentDetail />
        </Show>
      </Suspense>
    </div>
  )
}
