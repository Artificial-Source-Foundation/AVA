# Cron Scheduler

> Status: Idea (not implemented)
> Source: Original
> Effort: Low

## Summary
A simple cron-based task scheduler that stores recurring agent tasks with standard 5-field cron expressions (minute, hour, day-of-month, month, day-of-week). Tasks can be added, removed, listed, and queried for their next run time.

## Key Design Points
- `ScheduledTask` with name, cron expression, command, and enabled flag
- `CronFields` parsed from standard 5-field cron format
- Validation: each field must contain only digits, `*`, `-`, `/`, or `,`
- `TaskScheduler` manages tasks in a HashMap by name
- `next_run_time` produces a human-readable description of when a task should next fire

## Integration Notes
- Would need a background tokio task to poll the scheduler and trigger agent runs
- Tasks could be persisted to SQLite alongside sessions
- The `/later` and post-complete messaging system may partially overlap with this use case
