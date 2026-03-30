import { type Component, createMemo, lazy, Match, Suspense, Switch } from 'solid-js'
import { useHq } from '../../stores/hq'

const HqDashboard = lazy(() => import('./screens/HqDashboard'))
const HqDirectorChat = lazy(() => import('./screens/HqDirectorChat'))
const HqTeam = lazy(() => import('./screens/HqOrgChart'))
const HqPlanReview = lazy(() => import('./screens/HqPlanReview'))

export const HqContent: Component = () => {
  const { hqPage } = useHq()

  const canonicalPage = createMemo(() => {
    const page = hqPage()
    if (page === 'agent-detail' || page === 'org-chart' || page === 'team') return 'team'
    if (
      page === 'epic-detail' ||
      page === 'issue-detail' ||
      page === 'issues' ||
      page === 'epics'
    ) {
      return 'director-chat'
    }
    return page
  })

  return (
    <div class="min-w-0 flex-1 overflow-hidden">
      <Suspense
        fallback={
          <div
            class="flex h-full items-center justify-center"
            style={{ color: 'var(--text-muted)' }}
          >
            Loading HQ...
          </div>
        }
      >
        <Switch>
          <Match when={canonicalPage() === 'director-chat'}>
            <HqDirectorChat />
          </Match>
          <Match when={canonicalPage() === 'dashboard'}>
            <HqDashboard />
          </Match>
          <Match when={canonicalPage() === 'team'}>
            <HqTeam />
          </Match>
          <Match when={canonicalPage() === 'plan-review'}>
            <HqPlanReview />
          </Match>
        </Switch>
      </Suspense>
    </div>
  )
}
