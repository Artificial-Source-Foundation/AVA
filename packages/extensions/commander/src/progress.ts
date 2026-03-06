import type { PraxisMode } from './mode-selector.js'

export interface PraxisProgress {
  mode: PraxisMode
  leads: LeadProgress[]
}

export interface LeadProgress {
  id: string
  domain: string
  status: 'pending' | 'active' | 'complete' | 'failed'
  engineers: EngineerProgress[]
}

export interface EngineerProgress {
  id: string
  task: string
  status: 'coding' | 'reviewing' | 'approved' | 'merging' | 'complete' | 'failed'
  reviewAttempts: number
}

const INITIAL_PROGRESS: PraxisProgress = {
  mode: 'light',
  leads: [],
}

export class PraxisProgressTracker {
  private progress: PraxisProgress = structuredClone(INITIAL_PROGRESS)

  handleEvent(event: Record<string, unknown>): boolean {
    const type = String(event.type ?? '')
    if (type === 'praxis:mode-selected') {
      this.progress.mode = (event.mode as PraxisMode) ?? 'light'
      return true
    }

    if (type === 'praxis:lead-assigned') {
      const leadId = String(event.childAgentId ?? event.leadId ?? '')
      const domain = String(event.domain ?? 'general')
      if (!this.progress.leads.some((lead) => lead.id === leadId)) {
        this.progress.leads.push({ id: leadId, domain, status: 'active', engineers: [] })
      }
      return true
    }

    if (type === 'praxis:engineer-spawned') {
      const lead = this.progress.leads[0]
      if (!lead) return false
      lead.engineers.push({
        id: String(event.childAgentId ?? ''),
        task: String(event.task ?? 'assigned task'),
        status: 'coding',
        reviewAttempts: 0,
      })
      return true
    }

    if (type === 'praxis:review-requested' || type === 'praxis:review-complete') {
      const engineerId = String(event.agentId ?? event.childAgentId ?? '')
      const engineer = this.findEngineer(engineerId)
      if (!engineer) return false
      if (type === 'praxis:review-requested') {
        engineer.status = 'reviewing'
        engineer.reviewAttempts += 1
      } else if (event.approved === true) {
        engineer.status = 'approved'
      }
      return true
    }

    if (type === 'praxis:merge-complete') {
      for (const lead of this.progress.leads) {
        for (const engineer of lead.engineers) {
          if (engineer.status === 'approved') engineer.status = 'complete'
        }
        lead.status = lead.engineers.every((engineer) => engineer.status === 'complete')
          ? 'complete'
          : lead.status
      }
      return true
    }

    return false
  }

  getProgress(): PraxisProgress {
    return structuredClone(this.progress)
  }

  private findEngineer(id: string): EngineerProgress | undefined {
    for (const lead of this.progress.leads) {
      const engineer = lead.engineers.find((entry) => entry.id === id)
      if (engineer) return engineer
    }
    return undefined
  }
}
