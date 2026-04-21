---
title: "Incident Response"
description: "Basic incident-handling workflow for runtime, benchmark, and release issues in AVA."
order: 2
updated: "2026-04-10"
---

# Incident Response

When a serious issue appears:

1. reproduce it as narrowly as possible
2. classify the responsible layer
3. fix the narrowest responsible layer
4. add or tighten regression coverage
5. rerun the affected verification path

Typical layers:

1. prompt issue
2. tool/runtime bug
3. provider/model issue
4. validation gap
5. benchmark/reporting issue
6. product-surface issue
