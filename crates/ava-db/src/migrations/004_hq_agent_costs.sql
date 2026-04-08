-- Historical HQ compatibility migration.
--
-- Retained because older databases may already include `hq_agents` rows. New
-- core features should not extend the HQ schema through this path.

ALTER TABLE hq_agents ADD COLUMN total_cost_usd REAL NOT NULL DEFAULT 0;
