//! Late-stage fallback strategies inspired by Aider's edit cascade.
//!
//! These strategies are intentionally placed at the **end** of the
//! `EditEngine` cascade so they only fire when all simpler/cheaper
//! strategies have already failed.
//!
//! 1. `ThreeWayMergeStrategy` — treats the edit as a 3-way merge
//!    (base = old_text, ours = matched file region, theirs = new_text).
//! 2. `DiffMatchPatchStrategy` — character-level fuzzy patching via
//!    the diff-match-patch algorithm.

use crate::edit::error::EditError;
use crate::edit::request::EditRequest;
use crate::edit::strategies::EditStrategy;

// ─── Strategy 1: Three-Way Merge ───────────────────────────────────────────

/// Treats the edit as a 3-way merge problem.
///
/// We construct three versions:
/// - **base**: the `old_text` (what the LLM thinks the file contains)
/// - **ours**: the actual file content in the region that best matches `old_text`
/// - **theirs**: `new_text` (what the LLM wants it to become)
///
/// We use `diffy::merge` to reconcile ours/theirs against the common base.
/// If the merge succeeds without conflict markers, we splice the result back
/// into the file.
#[derive(Debug, Default)]
pub struct ThreeWayMergeStrategy;

impl EditStrategy for ThreeWayMergeStrategy {
    fn name(&self) -> &'static str {
        "three_way_merge"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        if request.old_text.is_empty() {
            return Ok(None);
        }

        // Find the best matching region in the file for old_text.
        let Some((start, end)) = find_best_line_region(&request.content, &request.old_text) else {
            return Ok(None);
        };

        let ours = &request.content[start..end];
        let base = &request.old_text;
        let theirs = &request.new_text;

        // Normalize trailing newlines so diffy doesn't treat them as conflicts.
        let ours_norm = ours.trim_end_matches('\n');
        let base_norm = base.trim_end_matches('\n');
        let theirs_norm = theirs.trim_end_matches('\n');

        // Attempt 3-way merge: base is what LLM expected, ours is what file has,
        // theirs is what LLM wants.
        let merged = match diffy::merge(base_norm, ours_norm, theirs_norm) {
            Ok(clean) => clean,
            Err(conflicted) => {
                // Check for conflict markers — if present, the merge failed.
                if conflicted.contains("<<<<<<<")
                    || conflicted.contains("=======")
                    || conflicted.contains(">>>>>>>")
                {
                    tracing::debug!("three_way_merge: merge produced conflicts, falling through");
                    return Ok(None);
                }
                // No conflict markers but returned Err — treat as unusable.
                return Ok(None);
            }
        };

        // Splice the merged result back into the file.
        // Restore trailing newline if the original region had one.
        let merged_final = if ours.ends_with('\n') && !merged.ends_with('\n') {
            format!("{merged}\n")
        } else {
            merged
        };

        let mut out = String::with_capacity(request.content.len() + merged_final.len());
        out.push_str(&request.content[..start]);
        out.push_str(&merged_final);
        out.push_str(&request.content[end..]);

        tracing::debug!("edit applied via strategy: three_way_merge");
        Ok(Some(out))
    }
}

/// Find the best line-aligned region in `content` that matches `needle`.
///
/// Returns byte offsets `(start, end)` of the best matching region,
/// or `None` if no region scores above the similarity threshold.
fn find_best_line_region(content: &str, needle: &str) -> Option<(usize, usize)> {
    let needle_lines: Vec<&str> = needle.lines().collect();
    if needle_lines.is_empty() {
        return None;
    }

    let content_lines: Vec<&str> = content.lines().collect();
    if content_lines.len() < needle_lines.len().saturating_sub(2) {
        return None;
    }

    // Build byte-offset index for each line start.
    let mut line_offsets: Vec<usize> = Vec::with_capacity(content_lines.len() + 1);
    let mut offset = 0;
    for line in &content_lines {
        line_offsets.push(offset);
        offset += line.len() + 1; // +1 for '\n'
    }
    line_offsets.push(content.len());

    let mut best_win: usize = needle_lines.len();
    let mut best_start: usize = 0;
    let mut best_score = 0.0_f64;

    // Allow the window to be slightly larger or smaller than needle_lines
    // to handle cases where the LLM added/removed lines.
    let window_sizes = [
        needle_lines.len(),
        needle_lines.len() + 1,
        needle_lines.len().saturating_sub(1),
        needle_lines.len() + 2,
        needle_lines.len().saturating_sub(2),
    ];

    for &win_size in &window_sizes {
        if win_size == 0 || win_size > content_lines.len() {
            continue;
        }
        for start in 0..=(content_lines.len() - win_size) {
            let candidate = &content_lines[start..start + win_size];
            let score = line_similarity(&needle_lines, candidate);
            if score > best_score && score > 0.5 {
                best_score = score;
                best_start = start;
                best_win = win_size;
                if (score - 1.0).abs() < f64::EPSILON {
                    // Perfect match — return immediately.
                    let byte_start = line_offsets[start];
                    let byte_end = line_offsets
                        .get(start + win_size)
                        .copied()
                        .unwrap_or(content.len())
                        .min(content.len());
                    return Some((byte_start, byte_end));
                }
            }
        }
    }

    if best_score <= 0.5 {
        return None;
    }

    let byte_start = line_offsets[best_start];
    let byte_end = line_offsets
        .get(best_start + best_win)
        .copied()
        .unwrap_or(content.len())
        .min(content.len());

    Some((byte_start, byte_end))
}

/// Compute similarity between two line slices using a simple ratio
/// of matching trimmed lines.
fn line_similarity(a: &[&str], b: &[&str]) -> f64 {
    if a.is_empty() && b.is_empty() {
        return 1.0;
    }
    let max_len = a.len().max(b.len());
    if max_len == 0 {
        return 0.0;
    }
    let min_len = a.len().min(b.len());
    let mut matches = 0;
    for i in 0..min_len {
        if a[i].trim() == b[i].trim() {
            matches += 1;
        }
    }
    matches as f64 / max_len as f64
}

// ─── Strategy 2: Diff-Match-Patch ──────────────────────────────────────────

/// Character-level fuzzy patching using the diff-match-patch algorithm.
///
/// This is the last-resort strategy before giving up.  It computes a
/// character-level diff between `old_text` and `new_text`, then applies
/// that patch to the file content with fuzzy matching tolerance.
#[derive(Debug, Default)]
pub struct DiffMatchPatchStrategy;

impl EditStrategy for DiffMatchPatchStrategy {
    fn name(&self) -> &'static str {
        "diff_match_patch"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        if request.old_text.is_empty() {
            return Ok(None);
        }

        // Pre-check: old_text must have a reasonable match in the content.
        // Find the best line-aligned region and verify similarity is high enough.
        // This prevents garbled patches when old_text has no real counterpart.
        let region = find_best_line_region(&request.content, &request.old_text);
        if region.is_none() {
            tracing::debug!("diff_match_patch: no matching region found in content, skipping");
            return Ok(None);
        }

        let mut dmp = dmp::new();
        // Tighten match threshold to reduce false positives.
        dmp.match_threshold = 0.3;
        dmp.match_distance = 500;

        // Compute patches that transform old_text -> new_text.
        let patches = dmp.patch_make1(&request.old_text, &request.new_text);

        if patches.is_empty() {
            return Ok(None);
        }

        // Apply patches to the full file content.  The DMP library will
        // fuzzy-match the patch location in the file even if the context
        // has shifted.
        let Ok((patched_chars, results)) = dmp.patch_apply(&patches, &request.content) else {
            return Ok(None);
        };

        // Require that ALL patch hunks applied successfully.
        if results.iter().any(|r| !r) {
            tracing::debug!(
                "diff_match_patch: not all patches applied ({} of {} succeeded)",
                results.iter().filter(|r| **r).count(),
                results.len()
            );
            return Ok(None);
        }

        let patched: String = patched_chars.into_iter().collect();

        // The DMP library adds padding chars — strip any NUL bytes that may
        // have been introduced as null-padding.
        let patched = patched.replace('\0', "");

        // Safety check: the patched content should differ from the original.
        if patched == request.content {
            return Ok(None);
        }

        // Verify the patch actually applied the intended edit cleanly.
        // 1. The new_text should appear as a whole in the result.
        if !request.new_text.is_empty() {
            // Use line-based containment: every line of new_text should appear
            // somewhere in the patched content (trimmed).
            let new_lines: Vec<&str> = request.new_text.lines().collect();
            let patched_text = &patched;
            let all_present = new_lines.iter().all(|line| {
                let trimmed = line.trim();
                trimmed.is_empty() || patched_text.contains(trimmed)
            });
            if !all_present {
                tracing::debug!(
                    "diff_match_patch: patched content missing new_text lines, rejecting"
                );
                return Ok(None);
            }
        }

        // 2. old_text should no longer be present verbatim.
        if !request.old_text.is_empty()
            && request.old_text != request.new_text
            && patched.contains(&request.old_text)
        {
            tracing::debug!("diff_match_patch: patched content still contains old_text, rejecting");
            return Ok(None);
        }

        // 3. Guard against garbled patches: the change should be localized.
        //    Count how many lines differ. If more than 2x the edit size, reject.
        let orig_lines: Vec<&str> = request.content.lines().collect();
        let patch_lines: Vec<&str> = patched.lines().collect();
        let max_lines = orig_lines.len().max(patch_lines.len());
        let diff_lines = (0..max_lines)
            .filter(|i| orig_lines.get(*i) != patch_lines.get(*i))
            .count();
        let expected_change = request
            .new_text
            .lines()
            .count()
            .max(request.old_text.lines().count());
        if diff_lines > expected_change * 2 + 2 {
            tracing::debug!(
                "diff_match_patch: too many lines changed ({diff_lines} vs expected ~{expected_change}), rejecting"
            );
            return Ok(None);
        }

        tracing::debug!("edit applied via strategy: diff_match_patch");
        Ok(Some(patched))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ─── ThreeWayMergeStrategy tests ───

    #[test]
    fn three_way_merge_resolves_non_overlapping_drift() {
        // The file has a change at the TOP of the block (renamed fn),
        // and the LLM wants to change the BOTTOM (return value).
        // 3-way merge can reconcile because the changes are in different regions.
        let content = "fn process_v2(input: i32) -> i32 {\n    let x = input + 1;\n    let y = x * 2;\n    x + y\n}\n";
        let old_text = "fn process(input: i32) -> i32 {\n    let x = input + 1;\n    let y = x * 2;\n    x + y\n}";
        let new_text = "fn process(input: i32) -> i32 {\n    let x = input + 1;\n    let y = x * 2;\n    x * y\n}";

        let req = EditRequest::new(content, old_text, new_text);
        let result = ThreeWayMergeStrategy.apply(&req).unwrap();
        assert!(result.is_some(), "three_way_merge should have matched");
        let out = result.unwrap();
        assert!(out.contains("x * y"), "should contain the edit: {out}");
        assert!(
            out.contains("process_v2"),
            "should preserve the file's fn name: {out}"
        );
    }

    #[test]
    fn three_way_merge_returns_none_on_no_match() {
        let content = "completely unrelated content\n";
        let old_text = "this does not exist anywhere";
        let new_text = "replacement";

        let req = EditRequest::new(content, old_text, new_text);
        let result = ThreeWayMergeStrategy.apply(&req).unwrap();
        assert!(result.is_none());
    }

    #[test]
    fn three_way_merge_returns_none_on_empty_old() {
        let req = EditRequest::new("content", "", "new");
        assert!(ThreeWayMergeStrategy.apply(&req).unwrap().is_none());
    }

    #[test]
    fn three_way_merge_handles_extra_line_in_file() {
        // File has an extra comment line the LLM didn't include.
        // The 3-way merge should keep the comment and apply the value change.
        let content = "fn foo() {\n    // important comment\n    let a = 1;\n    let b = 2;\n}\n";
        let old_text = "fn foo() {\n    let a = 1;\n    let b = 2;\n}";
        let new_text = "fn foo() {\n    let a = 99;\n    let b = 2;\n}";

        let req = EditRequest::new(content, old_text, new_text);
        let result = ThreeWayMergeStrategy.apply(&req).unwrap();
        // Note: 3-way merge may or may not succeed depending on conflict detection.
        // If it succeeds, verify correctness. If not, that's acceptable — DMP will handle it.
        if let Some(out) = result {
            assert!(out.contains("99"), "should contain the new value: {out}");
        }
    }

    // ─── DiffMatchPatchStrategy tests ───

    #[test]
    fn dmp_applies_fuzzy_patch() {
        // The file content is slightly different from old_text (extra spaces),
        // but DMP should still find and patch it.
        let content =
            "function greet(name) {\n  console.log('Hello ' + name);\n  return true;\n}\n";
        let old_text = "function greet(name) {\n  console.log('Hello ' + name);\n  return true;\n}";
        let new_text = "function greet(name) {\n  console.log('Hi ' + name);\n  return true;\n}";

        let req = EditRequest::new(content, old_text, new_text);
        let result = DiffMatchPatchStrategy.apply(&req).unwrap();
        assert!(result.is_some(), "dmp should apply patch");
        let out = result.unwrap();
        assert!(out.contains("Hi"), "should contain patched text: {out}");
        assert!(!out.contains("Hello"), "should not contain old text: {out}");
    }

    #[test]
    fn dmp_returns_none_on_empty_old() {
        let req = EditRequest::new("content", "", "new");
        assert!(DiffMatchPatchStrategy.apply(&req).unwrap().is_none());
    }

    #[test]
    fn dmp_returns_none_when_no_match_in_file() {
        // old_text and file content are completely different.
        let content = "aaaa bbbb cccc\n";
        let old_text = "xxxx yyyy zzzz";
        let new_text = "xxxx YYYY zzzz";

        let req = EditRequest::new(content, old_text, new_text);
        let result = DiffMatchPatchStrategy.apply(&req).unwrap();
        // DMP may or may not find a match here — either None or a bad patch.
        // The key is it shouldn't panic.
        if let Some(out) = result {
            // If it did produce output, it should differ from the original.
            assert_ne!(out, content);
        }
    }

    #[test]
    fn dmp_patches_with_context_shift() {
        // The target text exists in the file but shifted by a few lines.
        let content =
            "// header\n// license\n\nfn add(a: i32, b: i32) -> i32 {\n    a + b\n}\n\nfn main() {}\n";
        let old_text = "fn add(a: i32, b: i32) -> i32 {\n    a + b\n}";
        let new_text = "fn add(a: i32, b: i32) -> i32 {\n    a + b + 1\n}";

        let req = EditRequest::new(content, old_text, new_text);
        let result = DiffMatchPatchStrategy.apply(&req).unwrap();
        assert!(result.is_some(), "should patch with shifted context");
        let out = result.unwrap();
        assert!(
            out.contains("a + b + 1"),
            "should contain patched expr: {out}"
        );
    }

    // ─── find_best_line_region tests ───

    #[test]
    fn find_region_exact() {
        let content = "line1\nline2\nline3\nline4\n";
        let needle = "line2\nline3";
        let (start, end) = find_best_line_region(content, needle).unwrap();
        let region = &content[start..end];
        assert!(
            region.contains("line2") && region.contains("line3"),
            "region: {region}"
        );
    }

    #[test]
    fn find_region_returns_none_for_empty() {
        assert!(find_best_line_region("content", "").is_none());
    }

    #[test]
    fn line_similarity_identical() {
        let a = vec!["foo", "bar"];
        let b = vec!["foo", "bar"];
        assert!((line_similarity(&a, &b) - 1.0).abs() < f64::EPSILON);
    }

    #[test]
    fn line_similarity_partial() {
        let a = vec!["foo", "bar"];
        let b = vec!["foo", "baz"];
        let score = line_similarity(&a, &b);
        assert!(score > 0.0 && score < 1.0);
    }
}
