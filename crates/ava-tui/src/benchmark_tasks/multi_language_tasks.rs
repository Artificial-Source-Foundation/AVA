use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

/// Returns Python benchmark tasks.
pub fn python_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "py_two_sum",
            prompt: "Write a Python function `two_sum(nums: list[int], target: int) -> list[int]` \
                     that returns indices of two numbers that add up to target. \
                     Only output the function code."
                .to_string(),
            expected_patterns: vec![r"def two_sum", r"(?i)(dict|hash|map|\{\})"],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
import unittest
class TestTwoSum(unittest.TestCase):
    def test_basic(self):
        self.assertEqual(sorted(two_sum([2,7,11,15], 9)), [0, 1])
    def test_negative(self):
        self.assertEqual(sorted(two_sum([-1,-2,-3,-4,-5], -8)), [2, 4])
    def test_duplicate(self):
        self.assertEqual(sorted(two_sum([3,3], 6)), [0, 1])
if __name__ == '__main__':
    unittest.main()
"#,
                setup_code: None,
                test_count: 3,
                language: Language::Python,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "py_flatten_nested",
            prompt: "Write a Python function `flatten(nested: list) -> list` that recursively \
                     flattens arbitrarily nested lists. For example, \
                     flatten([1, [2, [3, 4], 5], 6]) should return [1, 2, 3, 4, 5, 6]. \
                     Only output the function code."
                .to_string(),
            expected_patterns: vec![r"def flatten", r"(?i)(isinstance|type.*list|recursive)"],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
import unittest
class TestFlatten(unittest.TestCase):
    def test_basic(self):
        self.assertEqual(flatten([1, [2, [3, 4], 5], 6]), [1, 2, 3, 4, 5, 6])
    def test_empty(self):
        self.assertEqual(flatten([]), [])
    def test_deep(self):
        self.assertEqual(flatten([[[1]], [[2]], [[3]]]), [1, 2, 3])
    def test_mixed(self):
        self.assertEqual(flatten([1, 2, 3]), [1, 2, 3])
if __name__ == '__main__':
    unittest.main()
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Python,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "py_async_rate_limiter",
            prompt: "Write a Python class `RateLimiter` that limits function calls to N calls \
                     per second using asyncio. It should have methods \
                     `__init__(self, max_calls: int)` and `async def acquire(self)` that blocks \
                     until a slot is available. Only output the code."
                .to_string(),
            expected_patterns: vec![r"class RateLimiter", r"async", r"(?i)(asyncio|await)"],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
import asyncio, time, unittest
class TestRateLimiter(unittest.TestCase):
    def test_basic(self):
        async def run():
            rl = RateLimiter(5)
            start = time.monotonic()
            for _ in range(5):
                await rl.acquire()
            elapsed = time.monotonic() - start
            self.assertLess(elapsed, 0.5)
        asyncio.run(run())
    def test_rate_limit(self):
        async def run():
            rl = RateLimiter(2)
            start = time.monotonic()
            for _ in range(4):
                await rl.acquire()
            elapsed = time.monotonic() - start
            self.assertGreater(elapsed, 0.9)
        asyncio.run(run())
if __name__ == '__main__':
    unittest.main()
"#,
                setup_code: None,
                test_count: 2,
                language: Language::Python,
            }),
            expected_min_tools: None,
        },
    ]
}

/// Returns TypeScript/JavaScript benchmark tasks.
///
/// Uses JavaScript (`.js` + `node`) for simpler execution without needing tsc.
pub fn typescript_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "js_debounce",
            prompt: "Write a JavaScript function `debounce(fn, ms)` that returns a debounced \
                     version of fn that delays invocation until ms milliseconds have passed \
                     since the last call. Only output the function code."
                .to_string(),
            expected_patterns: vec![
                r"(?i)(function debounce|const debounce)",
                r"(?i)(setTimeout|clearTimeout)",
            ],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
const assert = require('assert');
async function test() {
    let count = 0;
    const fn = debounce(() => count++, 50);
    fn(); fn(); fn();
    await new Promise(r => setTimeout(r, 100));
    assert.strictEqual(count, 1, 'Should only call once');

    fn();
    await new Promise(r => setTimeout(r, 100));
    assert.strictEqual(count, 2, 'Should call again after delay');
    console.log('All tests passed');
}
test().catch(e => { console.error(e); process.exit(1); });
"#,
                setup_code: None,
                test_count: 2,
                language: Language::JavaScript,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "js_deep_clone",
            prompt: "Write a JavaScript function `deepClone(obj)` that creates a deep copy of \
                     an object, handling nested objects, arrays, Date, RegExp, Map, and Set. \
                     Do not use JSON.parse/JSON.stringify. Only output the function code."
                .to_string(),
            expected_patterns: vec![
                r"(?i)(function deepClone|const deepClone)",
                r"(?i)(typeof|instanceof)",
                r"(?i)(Array|Object)",
            ],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
const assert = require('assert');
const original = { a: 1, b: { c: [1,2,3], d: new Date('2024-01-01') }, e: /test/gi };
const cloned = deepClone(original);
assert.deepStrictEqual(cloned.b.c, [1,2,3]);
assert.notStrictEqual(cloned.b.c, original.b.c);
assert.notStrictEqual(cloned.b, original.b);
cloned.b.c.push(4);
assert.strictEqual(original.b.c.length, 3, 'Original should be unchanged');
assert.ok(cloned.b.d instanceof Date);
assert.ok(cloned.e instanceof RegExp);
console.log('All tests passed');
"#,
                setup_code: None,
                test_count: 6,
                language: Language::JavaScript,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "js_react_component",
            prompt: "Write a React functional component called `SearchFilter` that takes props \
                     `items: string[]` and `onSelect: (item: string) => void`. It should render \
                     an input field for filtering and a list of matching items. Clicking an item \
                     calls onSelect. Use useState for the filter state. Only output the component \
                     code (assume React is imported)."
                .to_string(),
            expected_patterns: vec![
                r"(?i)(function SearchFilter|const SearchFilter)",
                r"useState",
                r"(?i)(onChange|filter)",
                r"(?i)(onClick|onSelect)",
            ],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: None,
            expected_min_tools: None,
        },
    ]
}

/// Returns Go benchmark tasks.
pub fn go_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "go_reverse_linked_list",
            prompt: "Write a Go function `ReverseList(head *ListNode) *ListNode` that reverses \
                     a singly linked list in-place. Define the ListNode struct as well. \
                     Only output the code."
                .to_string(),
            expected_patterns: vec![r"func ReverseList", r"ListNode", r"Next"],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
func listToSlice(head *ListNode) []int {
    var result []int
    for head != nil {
        result = append(result, head.Val)
        head = head.Next
    }
    return result
}
func sliceToList(vals []int) *ListNode {
    dummy := &ListNode{}
    curr := dummy
    for _, v := range vals {
        curr.Next = &ListNode{Val: v}
        curr = curr.Next
    }
    return dummy.Next
}
func assertEqual(got, want []int) {
    if len(got) != len(want) {
        fmt.Printf("FAIL: got %v, want %v\n", got, want)
        os.Exit(1)
    }
    for i := range got {
        if got[i] != want[i] {
            fmt.Printf("FAIL: got %v, want %v\n", got, want)
            os.Exit(1)
        }
    }
}
func main() {
    l1 := sliceToList([]int{1, 2, 3, 4, 5})
    r1 := listToSlice(ReverseList(l1))
    assertEqual(r1, []int{5, 4, 3, 2, 1})

    r2 := listToSlice(ReverseList(nil))
    assertEqual(r2, []int{})

    l3 := sliceToList([]int{1})
    r3 := listToSlice(ReverseList(l3))
    assertEqual(r3, []int{1})

    fmt.Println("All tests passed")
}
"#,
                setup_code: None,
                test_count: 3,
                language: Language::Go,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "go_concurrent_map",
            prompt: "Write a Go type `SafeMap` that is a goroutine-safe map[string]interface{} \
                     using sync.RWMutex. Implement methods Get(key) (value, ok), \
                     Set(key, value), Delete(key), and Len() int. Also write a \
                     `NewSafeMap() *SafeMap` constructor. Only output the code."
                .to_string(),
            expected_patterns: vec![
                r"(?i)(SafeMap|safeMap)",
                r"(?i)(RWMutex|sync\.Mutex)",
                r"(?i)(func.*Get|func.*Set)",
            ],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
func assert(cond bool, msg string) {
    if !cond {
        fmt.Println("FAIL:", msg)
        os.Exit(1)
    }
}
func main() {
    m := NewSafeMap()
    m.Set("a", 1)
    m.Set("b", "hello")

    v, ok := m.Get("a")
    assert(ok, "key 'a' should exist")
    assert(v.(int) == 1, "value should be 1")
    assert(m.Len() == 2, "length should be 2")

    m.Delete("a")
    _, ok = m.Get("a")
    assert(!ok, "key 'a' should be deleted")
    assert(m.Len() == 1, "length should be 1")

    var wg sync.WaitGroup
    for i := 0; i < 100; i++ {
        wg.Add(1)
        go func(i int) {
            defer wg.Done()
            m.Set(fmt.Sprintf("key%d", i), i)
        }(i)
    }
    wg.Wait()
    assert(m.Len() == 101, "length should be 101 after concurrent writes")

    fmt.Println("All tests passed")
}
"#,
                setup_code: None,
                test_count: 6,
                language: Language::Go,
            }),
            expected_min_tools: None,
        },
    ]
}
