use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

/// Returns the default set of benchmark tasks.
pub fn default_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "is_palindrome",
            prompt: "Write a Rust function `is_palindrome(s: &str) -> bool` that checks if a string \
                     is a palindrome, ignoring case and non-alphanumeric characters. Only output the \
                     function code, no explanation needed."
                .to_string(),
            expected_patterns: vec![
                r"fn\s+is_palindrome",
                r"-> bool",
                r"(?i)(to_lowercase|to_ascii_lowercase|eq_ignore_ascii_case)",
            ],
            category: TaskCategory::Simple,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_simple_palindrome() {
        assert!(is_palindrome("racecar"));
    }

    #[test]
    fn test_not_palindrome() {
        assert!(!is_palindrome("hello"));
    }

    #[test]
    fn test_mixed_case_with_spaces() {
        assert!(is_palindrome("A man a plan a canal Panama"));
    }

    #[test]
    fn test_empty_string() {
        assert!(is_palindrome(""));
    }

    #[test]
    fn test_punctuation_ignored() {
        assert!(is_palindrome("No lemon, no melon"));
    }
}
"#,
                setup_code: None,
                test_count: 5,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "merge_sorted",
            prompt: "Write a Rust function `merge_sorted(a: &[i32], b: &[i32]) -> Vec<i32>` that \
                     merges two sorted slices into a single sorted vector in O(n+m) time. Only output \
                     the function code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"fn\s+merge_sorted", r"Vec<i32>", r"(&\[i32\]|&\[i32\])"],
            category: TaskCategory::Medium,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_merge() {
        assert_eq!(merge_sorted(&[1, 3, 5], &[2, 4, 6]), vec![1, 2, 3, 4, 5, 6]);
    }

    #[test]
    fn test_empty_first() {
        assert_eq!(merge_sorted(&[], &[1, 2]), vec![1, 2]);
    }

    #[test]
    fn test_empty_second() {
        assert_eq!(merge_sorted(&[1], &[]), vec![1]);
    }

    #[test]
    fn test_both_empty() {
        assert_eq!(merge_sorted(&[], &[]), Vec::<i32>::new());
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "lru_cache",
            prompt: "Write a Rust module with a `LruCache<K, V>` struct (where K: Eq + Hash + Clone, V: Clone) \
                     that supports `new(capacity: usize)`, \
                     `get(&mut self, key: &K) -> Option<V>`, and `put(&mut self, key: K, value: V)` operations. \
                     Use a HashMap and a Vec or VecDeque for ordering. Only output the code, \
                     no explanation needed. Do NOT use any external crates."
                .to_string(),
            expected_patterns: vec![
                r"(?i)struct\s+LRUCache|struct\s+LruCache",
                r"fn\s+(new|get|put)",
                r"HashMap",
            ],
            category: TaskCategory::Hard,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_put_get() {
        let mut cache = LruCache::new(2);
        cache.put(1, "a".to_string());
        cache.put(2, "b".to_string());
        assert_eq!(cache.get(&1), Some("a".to_string()));
        assert_eq!(cache.get(&2), Some("b".to_string()));
    }

    #[test]
    fn test_capacity_eviction() {
        let mut cache = LruCache::new(2);
        cache.put(1, "a".to_string());
        cache.put(2, "b".to_string());
        cache.put(3, "c".to_string());
        assert_eq!(cache.get(&1), None);
        assert_eq!(cache.get(&3), Some("c".to_string()));
    }

    #[test]
    fn test_get_updates_recency() {
        let mut cache = LruCache::new(2);
        cache.put(1, "a".to_string());
        cache.put(2, "b".to_string());
        cache.get(&1);
        cache.put(3, "c".to_string());
        assert_eq!(cache.get(&1), Some("a".to_string()));
        assert_eq!(cache.get(&2), None);
        assert_eq!(cache.get(&3), Some("c".to_string()));
    }
}
"#,
                setup_code: None,
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "bash_echo",
            prompt: "Use the bash tool to run `echo hello` and report the output.".to_string(),
            expected_patterns: vec![r"(?i)hello"],
            category: TaskCategory::ToolUse,
            needs_tools: true,
            test_harness: None,
            expected_min_tools: Some(1),
        },
        BenchmarkTask {
            name: "read_cargo",
            prompt: "Read the file Cargo.toml in the current directory and list all workspace members."
                .to_string(),
            expected_patterns: vec![r"(?i)(member|crate|workspace)"],
            category: TaskCategory::RealWorld,
            needs_tools: true,
            test_harness: None,
            expected_min_tools: Some(2),
        },
    ]
}

/// Returns advanced Rust benchmark tasks (Tier 2, compile+test).
pub fn advanced_rust_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "concurrent_counter",
            prompt: "Implement a thread-safe `Counter` struct in Rust with `increment()`, \
                     `decrement()`, and `get()` methods. It must be safe to use from multiple \
                     threads simultaneously. Include a function \
                     `parallel_increment(counter: &Counter, n: usize)` that spawns `n` threads, \
                     each incrementing the counter 1000 times, and waits for all to complete. \
                     Only output the code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"Arc|Mutex|AtomicUsize|Atomic", r"thread|spawn"],
            category: TaskCategory::Hard,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
use std::sync::Arc;
use std::thread;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_counter_starts_at_zero() {
        let counter = Counter::new();
        assert_eq!(counter.get(), 0);
    }

    #[test]
    fn test_increment_decrement() {
        let counter = Counter::new();
        counter.increment();
        counter.increment();
        counter.decrement();
        assert_eq!(counter.get(), 1);
    }

    #[test]
    fn test_parallel_increment() {
        let counter = Counter::new();
        parallel_increment(&counter, 4);
        assert_eq!(counter.get(), 4000);
    }
}
"#,
                setup_code: None,
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "iterator_adapter",
            prompt: "Implement a custom iterator adapter `Batched<I>` that yields `Vec<T>` batches \
                     of a given size from any iterator. Implement it as a method on a `BatchIterator` \
                     trait that extends `Iterator`. The last batch may be smaller than the batch size.\n\n\
                     Example:\n\
                     ```rust\n\
                     let v = vec![1,2,3,4,5];\n\
                     let batches: Vec<Vec<i32>> = v.into_iter().batched(2).collect();\n\
                     assert_eq!(batches, vec![vec![1,2], vec![3,4], vec![5]]);\n\
                     ```\n\n\
                     Only output the code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"Iterator|IntoIterator", r"impl|trait", r"Vec"],
            category: TaskCategory::Medium,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_empty_iterator() {
        let v: Vec<i32> = vec![];
        let batches: Vec<Vec<i32>> = v.into_iter().batched(2).collect();
        assert!(batches.is_empty());
    }

    #[test]
    fn test_exact_division() {
        let v = vec![1, 2, 3, 4];
        let batches: Vec<Vec<i32>> = v.into_iter().batched(2).collect();
        assert_eq!(batches, vec![vec![1, 2], vec![3, 4]]);
    }

    #[test]
    fn test_remainder() {
        let v = vec![1, 2, 3, 4, 5];
        let batches: Vec<Vec<i32>> = v.into_iter().batched(2).collect();
        assert_eq!(batches, vec![vec![1, 2], vec![3, 4], vec![5]]);
    }

    #[test]
    fn test_batch_size_one() {
        let v = vec![1, 2, 3];
        let batches: Vec<Vec<i32>> = v.into_iter().batched(1).collect();
        assert_eq!(batches, vec![vec![1], vec![2], vec![3]]);
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "binary_tree",
            prompt: "Implement a generic binary search tree `BST<T: Ord>` with \
                     `insert(&mut self, value: T)`, `contains(&self, value: &T) -> bool`, \
                     `min(&self) -> Option<&T>`, and `into_sorted_vec(self) -> Vec<T>` \
                     (in-order traversal). Use `Box<Node<T>>` for child pointers. \
                     Only output the code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"struct.*Node|BST", r"Box", r"Ord"],
            category: TaskCategory::Hard,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_insert_and_contains() {
        let mut bst = BST::new();
        bst.insert(5);
        bst.insert(3);
        bst.insert(7);
        assert!(bst.contains(&5));
        assert!(bst.contains(&3));
        assert!(bst.contains(&7));
        assert!(!bst.contains(&4));
    }

    #[test]
    fn test_min() {
        let mut bst = BST::new();
        assert_eq!(bst.min(), None);
        bst.insert(5);
        bst.insert(3);
        bst.insert(7);
        bst.insert(1);
        assert_eq!(bst.min(), Some(&1));
    }

    #[test]
    fn test_into_sorted_vec() {
        let mut bst = BST::new();
        bst.insert(5);
        bst.insert(3);
        bst.insert(7);
        bst.insert(1);
        bst.insert(9);
        assert_eq!(bst.into_sorted_vec(), vec![1, 3, 5, 7, 9]);
    }

    #[test]
    fn test_empty_tree() {
        let bst: BST<i32> = BST::new();
        assert!(!bst.contains(&1));
        assert_eq!(bst.into_sorted_vec(), Vec::<i32>::new());
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "state_machine",
            prompt: "Implement a simple state machine for a turnstile. States: `Locked` and \
                     `Unlocked`. Events: `Coin` and `Push`. Transitions: Locked+Coin->Unlocked, \
                     Unlocked+Push->Locked, Locked+Push->Locked (no change), \
                     Unlocked+Coin->Unlocked (no change). Implement `Turnstile::new()` \
                     (starts Locked), `process(&mut self, event: Event) -> &State`, and \
                     `state(&self) -> &State`. Only output the code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"enum.*State|Locked|Unlocked", r"enum.*Event|Coin|Push"],
            category: TaskCategory::Medium,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_initial_state() {
        let t = Turnstile::new();
        assert_eq!(*t.state(), State::Locked);
    }

    #[test]
    fn test_coin_unlocks() {
        let mut t = Turnstile::new();
        t.process(Event::Coin);
        assert_eq!(*t.state(), State::Unlocked);
    }

    #[test]
    fn test_push_locks() {
        let mut t = Turnstile::new();
        t.process(Event::Coin);
        t.process(Event::Push);
        assert_eq!(*t.state(), State::Locked);
    }

    #[test]
    fn test_sequence() {
        let mut t = Turnstile::new();
        t.process(Event::Push);
        assert_eq!(*t.state(), State::Locked);
        t.process(Event::Coin);
        assert_eq!(*t.state(), State::Unlocked);
        t.process(Event::Coin);
        assert_eq!(*t.state(), State::Unlocked);
        t.process(Event::Push);
        assert_eq!(*t.state(), State::Locked);
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
    ]
}
