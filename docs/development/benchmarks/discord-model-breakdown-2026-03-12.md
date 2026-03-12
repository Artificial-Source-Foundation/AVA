# Discord Benchmark Posts - New Model Releases (2026-03-12)

Copy/paste Discord drafts for separate per-model release posts.

These are written for community readers and potential users, not just benchmark nerds.

---

## Hunter Alpha

```text
## Hunter Alpha benchmark breakdown

We just finished a fresh round of AVA benchmarks, and **Hunter Alpha** came out looking like the best balanced model in the batch.

If you want the short version:

> Hunter is the model we would recommend first if you want the strongest combination of speed, reliability, and general coding usefulness.

Here is why.

### What we saw in the benchmarks

- In our 7-task polyglot run, Hunter reached **6/7 quality passes**
- It had the **best full-pass rate** in that run with **5/7 tasks fully passing**
- It was also the **fastest model in the polyglot run** at about **11.2s average total time** and **9.6s TTFT**
- In our 23-task Rust-heavy stress run, Hunter landed **18/23 quality passes** and **11/23 full passes**

That combination matters a lot.

Some models can look smart in evaluation but fail to convert that into fully working code. Hunter did a better job of closing that gap. It was not just "good on paper" - it turned a strong share of its attempts into passing outputs.

### What that means in practice

Hunter feels like the safest default if you want one model that can do a little bit of everything well.

It is:
- fast enough to feel responsive
- strong across multiple languages
- more likely than most to turn decent reasoning into code that actually runs

It did not win every single metric in every benchmark, but it consistently stayed near the top and had the cleanest overall profile.

### Who we think should use it

Use **Hunter Alpha** if you want:
- the best all-around default model
- a strong balance of quality and execution reliability
- a model that feels good for day-to-day coding instead of just benchmark screenshots

### Honest caveat

A few failures in this benchmark set were harness-related, including some noisy extraction issues and a missing Go benchmark file in one run. So individual numbers may move a little on rerun.

But the current signal is already pretty clear:

> **Hunter Alpha looks like the strongest general-purpose pick in this batch.**
```

---

## Healer Alpha

```text
## Healer Alpha benchmark breakdown

We also ran a fresh set of AVA benchmarks on **Healer Alpha**, and this one stood out for a different reason than Hunter.

If you want the short version:

> Healer looks like the strongest model in this batch when you care most about raw code quality, especially on hard Rust-heavy tasks.

### What we saw in the benchmarks

- In our 23-task Rust-heavy stress run, Healer delivered **19/23 quality passes**, which was the **best raw quality score in the field**
- In that same run, it reached **10/23 full passes**
- In the 7-task polyglot run, it still performed well with **6/7 quality passes** and **4/7 full passes**

So while Hunter looked like the best overall balance, Healer looked like the model with the strongest "this code is conceptually right" profile on the hardest benchmark set.

### What that means in practice

Healer often looked especially strong on correctness patterns, structure, and overall code quality, particularly in Rust.

The tradeoff is that it did not always convert that quality edge into the highest final pass count. In other words:

> Healer often looked like the smartest model in the room, even when it was not always the one with the cleanest end-to-end completion rate.

That is still a very valuable profile.

For users working on harder coding tasks, especially Rust-oriented ones, that extra quality can matter more than winning the speed chart.

### Who we think should use it

Use **Healer Alpha** if you want:
- the strongest raw code quality in this benchmark batch
- a model that shines on harder Rust-style tasks
- a model you would trust when correctness matters more than pure speed

### Honest caveat

Like the rest of this benchmark set, a few failures were noisy and harness-related, so we are not pretending every single raw pass/fail count is final.

But the shape of the result is already obvious:

> **Healer Alpha looks like the best quality-first model in this batch, especially for Rust-heavy work.**
```

---

## NVIDIA Nemotron 3 Super 120B A12B Free

```text
## NVIDIA Nemotron benchmark breakdown

One of the most interesting results from our latest AVA benchmark run was **NVIDIA Nemotron 3 Super 120B A12B Free**.

If you want the short version:

> Nemotron was the surprise contender - faster and more competitive than most people would expect from a free model.

### What we saw in the benchmarks

- In the 7-task polyglot run, Nemotron reached **6/7 quality passes**
- In the 23-task Rust-heavy run, it scored **15/23 quality passes**
- It tied Hunter for the **highest full-pass count** in that Rust run with **11/23 full passes**
- It was also the **fastest model in the Rust-heavy run** at about **23.0s average total time** and **13.6s TTFT**

That is a much stronger showing than a lot of people would assume from a free option.

### What that means in practice

Nemotron is not the best model overall in this group. It did not beat Healer on raw quality, and it did not beat Hunter on overall balance.

But it absolutely earned respect in this run.

It showed three things clearly:
- it can keep up surprisingly well on quality
- it can finish hard Rust tasks at a competitive rate
- it delivers that while being very fast for this benchmark set

That makes it more than just a fallback or "cheap backup" model.

### Who we think should use it

Use **NVIDIA Nemotron** if you want:
- the most interesting value pick in this batch
- a strong free or low-cost option
- a model that overdelivers relative to expectation, especially on speed

### Honest caveat

This benchmark set still had some harness noise, and Nemotron had a few extraction / formatting-related misses in the harder run.

Even with that noise, the signal is still strong:

> **Nemotron is a legitimate contender, not just a budget curiosity.**
```

---

## Notes

- These posts intentionally focus on the three new models you want to announce: Hunter, Healer, and NVIDIA Nemotron
- Trinity and GPT-OSS are omitted on purpose because this is framed as a per-model release series, not a full benchmark dump
- Numbers are based on the recent 7-task polyglot run and 23-task Rust-heavy run

---

## Copy/Paste Stats Blocks

Use these at the end of each Discord post if you want a more evidence-heavy version.

### Hunter Alpha - stats block

```text
### Benchmark snapshot

Polyglot run (7 tasks)
Quality pass rate   6/7    [#################---] 86%
Full pass rate      5/7    [##############------] 71%
Avg total time      11.2s  [fastest in run]
TTFT                9.6s   [fastest in run]

Rust-heavy run (23 tasks)
Quality pass rate   18/23  [################----] 78%
Full pass rate      11/23  [##########----------] 48%
Avg total time      38.3s
TTFT                20.0s

Comparison vs the other models
- Tied for best quality in the polyglot run at 6/7
- Best full-pass rate in the polyglot run at 5/7
- Best speed in the polyglot run
- In Rust, tied Nemotron for most full passes at 11/23
- Slightly behind Healer on raw Rust quality (18/23 vs 19/23)
```

### Healer Alpha - stats block

```text
### Benchmark snapshot

Polyglot run (7 tasks)
Quality pass rate   6/7    [#################---] 86%
Full pass rate      4/7    [###########---------] 57%
Avg total time      15.2s
TTFT                13.8s

Rust-heavy run (23 tasks)
Quality pass rate   19/23  [#################---] 83%
Full pass rate      10/23  [#########-----------] 43%
Avg total time      24.6s
TTFT                17.6s

Comparison vs the other models
- Tied for best quality in the polyglot run at 6/7
- Best raw quality score in the Rust-heavy run at 19/23
- Slightly behind Hunter and Nemotron on Rust full passes (10/23 vs 11/23)
- Faster than Hunter in Rust, slightly slower than Nemotron
- Best fit when quality matters more than pure execution rate
```

### NVIDIA Nemotron 3 Super 120B A12B Free - stats block

```text
### Benchmark snapshot

Polyglot run (7 tasks)
Quality pass rate   6/7    [#################---] 86%
Full pass rate      4/7    [###########---------] 57%
Avg total time      20.5s
TTFT                17.3s

Rust-heavy run (23 tasks)
Quality pass rate   15/23  [#############-------] 65%
Full pass rate      11/23  [##########----------] 48%
Avg total time      23.0s  [fastest in run]
TTFT                13.6s  [fastest in run]

Comparison vs the other models
- Tied for best quality in the polyglot run at 6/7
- Tied Hunter for the most Rust full passes at 11/23
- Fastest model in the Rust-heavy run
- Behind Hunter and Healer on raw Rust quality
- Strongest value/surprise profile in the batch
```

### Cross-model comparison block

```text
### Quick comparison

Polyglot quality
Hunter    6/7  [#################---] 86%
Healer    6/7  [#################---] 86%
Nemotron  6/7  [#################---] 86%

Polyglot full passes
Hunter    5/7  [##############------] 71%
Healer    4/7  [###########---------] 57%
Nemotron  4/7  [###########---------] 57%

Rust quality
Hunter    18/23 [################----] 78%
Healer    19/23 [#################---] 83%
Nemotron  15/23 [#############-------] 65%

Rust full passes
Hunter    11/23 [##########----------] 48%
Healer    10/23 [#########-----------] 43%
Nemotron  11/23 [##########----------] 48%

Rust avg total time
Nemotron  23.0s
Healer    24.6s
Hunter    38.3s

Best overall default: Hunter
Best raw Rust quality: Healer
Best value / surprise pick: Nemotron
```
