use std::cmp::{max, min};

use crate::edit::error::EditError;
use crate::edit::request::EditRequest;
use crate::edit::strategies::EditStrategy;

#[derive(Debug, Clone)]
pub struct StreamingMatcher {
    pub substitution_cost: u32,
    pub indel_cost: u32,
    pub max_distance: u32,
}

impl Default for StreamingMatcher {
    fn default() -> Self {
        Self {
            substitution_cost: 2,
            indel_cost: 1,
            max_distance: 8,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StreamMatch {
    pub chunk_index: usize,
    pub start: usize,
    pub end: usize,
    pub distance: u32,
    pub matched: String,
}

#[derive(Debug, Clone, Default)]
pub struct FuzzyMatchStrategy {
    matcher: StreamingMatcher,
}

impl FuzzyMatchStrategy {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_matcher(matcher: StreamingMatcher) -> Self {
        Self { matcher }
    }
}

impl EditStrategy for FuzzyMatchStrategy {
    fn name(&self) -> &'static str {
        "fuzzy_match"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        if request.old_text.is_empty() {
            return Ok(None);
        }
        let Some((start, end, _)) = self.matcher.best_span(&request.content, &request.old_text)
        else {
            return Ok(None);
        };

        let mut out = String::with_capacity(request.content.len() + request.new_text.len());
        out.push_str(&request.content[..start]);
        out.push_str(&request.new_text);
        out.push_str(&request.content[end..]);
        Ok(Some(out))
    }
}

impl StreamingMatcher {
    pub fn weighted_distance(&self, a: &str, b: &str) -> u32 {
        let ac: Vec<char> = a.chars().collect();
        let bc: Vec<char> = b.chars().collect();
        let m = ac.len();
        let n = bc.len();

        if m == 0 {
            return (n as u32) * self.indel_cost;
        }
        if n == 0 {
            return (m as u32) * self.indel_cost;
        }

        let mut dp = vec![vec![0_u32; n + 1]; m + 1];
        for (i, row) in dp.iter_mut().enumerate().take(m + 1) {
            row[0] = (i as u32) * self.indel_cost;
        }
        for (j, cell) in dp[0].iter_mut().enumerate().take(n + 1) {
            *cell = (j as u32) * self.indel_cost;
        }

        for i in 1..=m {
            for j in 1..=n {
                let sub = if ac[i - 1] == bc[j - 1] {
                    0
                } else {
                    self.substitution_cost
                };
                dp[i][j] = min(
                    dp[i - 1][j] + self.indel_cost,
                    min(dp[i][j - 1] + self.indel_cost, dp[i - 1][j - 1] + sub),
                );
            }
        }
        dp[m][n]
    }

    pub fn best_span(&self, content: &str, needle: &str) -> Option<(usize, usize, u32)> {
        if content.is_empty() || needle.is_empty() {
            return None;
        }

        let idx: Vec<usize> = content
            .char_indices()
            .map(|(i, _)| i)
            .chain(std::iter::once(content.len()))
            .collect();

        let needle_len = needle.chars().count();
        let min_len = max(1, needle_len * 6 / 10);
        let max_len = max(min_len, needle_len * 14 / 10 + 2);
        let allowed = min(self.max_distance, (needle_len as u32) / 3 + 2);

        let mut best: Option<(usize, usize, u32)> = None;

        for s in 0..idx.len().saturating_sub(1) {
            let end_cap = min(idx.len() - 1, s + max_len);
            let start_end = s + min_len;
            if start_end > end_cap {
                continue;
            }
            for e in start_end..=end_cap {
                let start = idx[s];
                let end = idx[e];
                let candidate = &content[start..end];
                let dist = self.weighted_distance(candidate, needle);
                if dist == 0 {
                    return Some((start, end, 0));
                }
                match best {
                    None => best = Some((start, end, dist)),
                    Some((_, _, d)) if dist < d => best = Some((start, end, dist)),
                    _ => {}
                }
            }
        }

        best.and_then(|b| if b.2 <= allowed { Some(b) } else { None })
    }

    pub fn match_stream<I>(&self, stream: I, needle: &str) -> Option<StreamMatch>
    where
        I: IntoIterator<Item = String>,
    {
        let allowed = min(self.max_distance, (needle.chars().count() as u32) / 3 + 2);
        for (chunk_index, chunk) in stream.into_iter().enumerate() {
            let Some((start, end, distance)) = self.best_span(&chunk, needle) else {
                continue;
            };
            if distance <= allowed {
                return Some(StreamMatch {
                    chunk_index,
                    start,
                    end,
                    distance,
                    matched: chunk[start..end].to_string(),
                });
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn weighted_distance_prefers_indel() {
        let m = StreamingMatcher::default();
        let sub = m.weighted_distance("abc", "axc");
        let indel = m.weighted_distance("abc", "abxc");
        assert!(sub >= indel);
    }

    #[test]
    fn fuzzy_strategy_applies_near_match() {
        let req = EditRequest::new("let total = count + 1;", "count +1", "count + 2");
        let out = FuzzyMatchStrategy::new().apply(&req).unwrap().unwrap();
        assert!(out.contains("count + 2"));
    }

    #[test]
    fn stream_match_finds_chunk() {
        let m = StreamingMatcher::default();
        let chunks = vec![
            "alpha beta".to_string(),
            "needle-ish content".to_string(),
            "omega".to_string(),
        ];
        let found = m.match_stream(chunks, "needle").unwrap();
        assert_eq!(found.chunk_index, 1);
    }
}
