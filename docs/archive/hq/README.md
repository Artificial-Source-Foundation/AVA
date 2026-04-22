# AVA HQ

> Crate: removed from the active workspace
> Status: historical/core-archival document only
> Active tracking: `../../architecture/plugin-boundary.md` and `../../project/backlog.md`

## Current Reality

1. HQ is no longer part of the active AVA 0.6 core product surface.
2. Core AVA no longer ships built-in HQ settings, sidebar, chat, desktop command, or web-route surfaces.
3. Any future HQ return should happen through plugin-owned registration points instead of reintroducing core-owned paths.

## What This Document Preserves

This file only keeps the minimum historical context that is still useful when working around old HQ code or planning a future pluginized HQ return.

## Historical HQ Shape

The old built-in HQ model centered on:

1. A persistent Director-style conversation.
2. Delegation to named specialist agents.
3. Plan review as an explicit trust boundary.
4. Repo-local HQ memory under `.ava/HQ/`.

That product shape is historical context, not current core guidance.

## Still-Relevant References

1. `crates/ava-db/src/migrations/003_hq.sql`
2. `crates/ava-db/src/migrations/004_hq_agent_costs.sql`
3. `../../architecture/plugin-boundary.md` — future plugin-boundary note

## Guidance

1. Do not treat this document as current product or UX guidance.
2. Do not restore old built-in HQ surfaces into core.
3. If HQ work resumes, route it through the plugin boundary and use shared AVA chat/settings primitives where possible.
