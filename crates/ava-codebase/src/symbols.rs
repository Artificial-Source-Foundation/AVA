//! Symbol extraction from source files using regex-based parsing.
//!
//! Extracts function, struct, trait, enum, class, and other symbol definitions
//! along with identifier references. Supports Rust, Python, JavaScript/TypeScript, and Go.

use std::collections::HashSet;
use std::sync::LazyLock;

use regex::Regex;

/// The kind of symbol definition.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SymbolKind {
    Function,
    Struct,
    Trait,
    Enum,
    Impl,
    TypeAlias,
    Const,
    Static,
    Method,
    Class,
    Interface,
    Module,
}

impl std::fmt::Display for SymbolKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Function => write!(f, "fn"),
            Self::Struct => write!(f, "struct"),
            Self::Trait => write!(f, "trait"),
            Self::Enum => write!(f, "enum"),
            Self::Impl => write!(f, "impl"),
            Self::TypeAlias => write!(f, "type"),
            Self::Const => write!(f, "const"),
            Self::Static => write!(f, "static"),
            Self::Method => write!(f, "method"),
            Self::Class => write!(f, "class"),
            Self::Interface => write!(f, "interface"),
            Self::Module => write!(f, "mod"),
        }
    }
}

/// A symbol definition found in a source file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Symbol {
    pub kind: SymbolKind,
    pub name: String,
    pub file_path: String,
    pub line: usize,
}

impl Symbol {
    /// Fully qualified name: `file_path::name`.
    pub fn fqn(&self) -> String {
        format!("{}::{}", self.file_path, self.name)
    }
}

/// A reference to an identifier in a source file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SymbolRef {
    pub name: String,
    pub file_path: String,
    pub line: usize,
}

// ── Rust patterns ──────────────────────────────────────────────────────────

static RUST_FN_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?m)^\s*(?:pub(?:\(.*?\))?\s+)?(?:async\s+)?fn\s+(\w+)").unwrap()
});
static RUST_STRUCT_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:pub(?:\(.*?\))?\s+)?struct\s+(\w+)").unwrap());
static RUST_TRAIT_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:pub(?:\(.*?\))?\s+)?trait\s+(\w+)").unwrap());
static RUST_ENUM_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:pub(?:\(.*?\))?\s+)?enum\s+(\w+)").unwrap());
static RUST_IMPL_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*impl(?:<[^>]*>)?\s+(\w+)").unwrap());
static RUST_TYPE_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:pub(?:\(.*?\))?\s+)?type\s+(\w+)").unwrap());
static RUST_CONST_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:pub(?:\(.*?\))?\s+)?const\s+(\w+)").unwrap());
static RUST_STATIC_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:pub(?:\(.*?\))?\s+)?static\s+(\w+)").unwrap());
static RUST_MOD_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:pub(?:\(.*?\))?\s+)?mod\s+(\w+)").unwrap());

// ── Python patterns ────────────────────────────────────────────────────────

static PY_DEF_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:async\s+)?def\s+(\w+)").unwrap());
static PY_CLASS_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*class\s+(\w+)").unwrap());

// ── JS/TS patterns ─────────────────────────────────────────────────────────

static JS_FN_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:export\s+)?(?:async\s+)?function\s+(\w+)").unwrap());
static JS_CLASS_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:export\s+)?class\s+(\w+)").unwrap());
static JS_CONST_FN_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?m)^\s*(?:export\s+)?(?:const|let)\s+(\w+)\s*=\s*(?:async\s+)?(?:\([^)]*\)|[a-zA-Z_]\w*)\s*=>").unwrap()
});
static TS_INTERFACE_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:export\s+)?interface\s+(\w+)").unwrap());
static TS_TYPE_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:export\s+)?type\s+(\w+)\s*[=<]").unwrap());

// ── Go patterns ────────────────────────────────────────────────────────────

static GO_FUNC_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"(?m)^func\s+(\w+)").unwrap());
static GO_METHOD_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^func\s+\([^)]+\)\s+(\w+)").unwrap());
static GO_TYPE_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^type\s+(\w+)\s+(?:struct|interface)").unwrap());

// ── Identifier reference pattern (language-agnostic) ───────────────────────

static IDENT_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\b([A-Z]\w{2,})\b").unwrap());

/// Extract symbol definitions and identifier references from source code.
///
/// Returns `(definitions, references)`.
pub fn extract_symbols(
    file_path: &str,
    content: &str,
    lang: &str,
) -> (Vec<Symbol>, Vec<SymbolRef>) {
    let definitions = match lang {
        "rs" => extract_rust_symbols(file_path, content),
        "py" => extract_python_symbols(file_path, content),
        "js" | "jsx" | "ts" | "tsx" => extract_js_symbols(file_path, content, lang),
        "go" => extract_go_symbols(file_path, content),
        _ => Vec::new(),
    };

    // Collect defined names so we can exclude them from references in this file
    let defined_names: HashSet<&str> = definitions.iter().map(|s| s.name.as_str()).collect();

    // Extract references: identifiers that start with uppercase and are 3+ chars
    // (heuristic to find type/struct/class references across files)
    let references = extract_references(file_path, content, &defined_names);

    (definitions, references)
}

fn line_number(content: &str, byte_offset: usize) -> usize {
    content[..byte_offset].matches('\n').count() + 1
}

fn extract_with_regex(re: &Regex, kind: SymbolKind, file_path: &str, content: &str) -> Vec<Symbol> {
    re.captures_iter(content)
        .filter_map(|cap| {
            let m = cap.get(1)?;
            let name = m.as_str().to_string();
            // Skip test-only or trivial names
            if name.starts_with('_') || name == "test" || name == "tests" {
                return None;
            }
            Some(Symbol {
                kind,
                name,
                file_path: file_path.to_string(),
                line: line_number(content, m.start()),
            })
        })
        .collect()
}

fn extract_rust_symbols(file_path: &str, content: &str) -> Vec<Symbol> {
    let mut syms = Vec::new();
    syms.extend(extract_with_regex(
        &RUST_FN_RE,
        SymbolKind::Function,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &RUST_STRUCT_RE,
        SymbolKind::Struct,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &RUST_TRAIT_RE,
        SymbolKind::Trait,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &RUST_ENUM_RE,
        SymbolKind::Enum,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &RUST_IMPL_RE,
        SymbolKind::Impl,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &RUST_TYPE_RE,
        SymbolKind::TypeAlias,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &RUST_CONST_RE,
        SymbolKind::Const,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &RUST_STATIC_RE,
        SymbolKind::Static,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &RUST_MOD_RE,
        SymbolKind::Module,
        file_path,
        content,
    ));
    syms
}

fn extract_python_symbols(file_path: &str, content: &str) -> Vec<Symbol> {
    let mut syms = Vec::new();
    syms.extend(extract_with_regex(
        &PY_DEF_RE,
        SymbolKind::Function,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &PY_CLASS_RE,
        SymbolKind::Class,
        file_path,
        content,
    ));
    syms
}

fn extract_js_symbols(file_path: &str, content: &str, lang: &str) -> Vec<Symbol> {
    let mut syms = Vec::new();
    syms.extend(extract_with_regex(
        &JS_FN_RE,
        SymbolKind::Function,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &JS_CLASS_RE,
        SymbolKind::Class,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &JS_CONST_FN_RE,
        SymbolKind::Function,
        file_path,
        content,
    ));
    if lang == "ts" || lang == "tsx" {
        syms.extend(extract_with_regex(
            &TS_INTERFACE_RE,
            SymbolKind::Interface,
            file_path,
            content,
        ));
        syms.extend(extract_with_regex(
            &TS_TYPE_RE,
            SymbolKind::TypeAlias,
            file_path,
            content,
        ));
    }
    syms
}

fn extract_go_symbols(file_path: &str, content: &str) -> Vec<Symbol> {
    let mut syms = Vec::new();
    syms.extend(extract_with_regex(
        &GO_FUNC_RE,
        SymbolKind::Function,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &GO_METHOD_RE,
        SymbolKind::Method,
        file_path,
        content,
    ));
    syms.extend(extract_with_regex(
        &GO_TYPE_RE,
        SymbolKind::Struct,
        file_path,
        content,
    ));
    syms
}

fn extract_references(file_path: &str, content: &str, exclude: &HashSet<&str>) -> Vec<SymbolRef> {
    let mut seen = HashSet::new();
    let mut refs = Vec::new();

    for cap in IDENT_RE.captures_iter(content) {
        let name = cap.get(1).unwrap().as_str();
        // Skip common false positives
        if is_common_word(name) || exclude.contains(name) {
            continue;
        }
        // Deduplicate per-file: only record each referenced name once
        if !seen.insert(name.to_string()) {
            continue;
        }
        refs.push(SymbolRef {
            name: name.to_string(),
            file_path: file_path.to_string(),
            line: line_number(content, cap.get(1).unwrap().start()),
        });
    }
    refs
}

/// Filter out common words that look like identifiers but aren't references.
fn is_common_word(name: &str) -> bool {
    matches!(
        name,
        "None"
            | "Some"
            | "Self"
            | "True"
            | "False"
            | "Error"
            | "Result"
            | "Option"
            | "String"
            | "Vec"
            | "HashMap"
            | "HashSet"
            | "Box"
            | "Arc"
            | "Mutex"
            | "RwLock"
            | "Rc"
            | "Cell"
            | "RefCell"
            | "Pin"
            | "Future"
            | "Iterator"
            | "Display"
            | "Debug"
            | "Default"
            | "Clone"
            | "Copy"
            | "Send"
            | "Sync"
            | "Sized"
            | "Drop"
            | "From"
            | "Into"
            | "AsRef"
            | "AsMut"
            | "Deref"
            | "DerefMut"
            | "Fn"
            | "FnMut"
            | "FnOnce"
            | "Read"
            | "Write"
            | "BufRead"
            | "Seek"
            | "Path"
            | "PathBuf"
            | "Object"
            | "Array"
            | "Function"
            | "Promise"
            | "Number"
            | "Boolean"
            | "Math"
            | "Date"
            | "RegExp"
            | "JSON"
            | "Map"
            | "Set"
            | "This"
            | "Console"
            | "Document"
            | "Window"
            | "TODO"
            | "FIXME"
            | "NOTE"
            | "SAFETY"
            | "IMPORTANT"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rust_symbols_extracted() {
        let code = r#"
pub struct Parser {
    tokens: Vec<Token>,
}

pub trait Visitor {
    fn visit(&self);
}

pub enum NodeKind {
    Leaf,
    Branch,
}

impl Parser {
    pub fn new() -> Self { todo!() }
    pub async fn parse(&self) -> Result<()> { todo!() }
}

type Callback = Box<dyn Fn()>;
const MAX_DEPTH: usize = 10;
mod utils;
"#;
        let (syms, _refs) = extract_symbols("src/parser.rs", code, "rs");
        let names: Vec<&str> = syms.iter().map(|s| s.name.as_str()).collect();
        assert!(names.contains(&"Parser"), "missing Parser: {names:?}");
        assert!(names.contains(&"Visitor"), "missing Visitor: {names:?}");
        assert!(names.contains(&"NodeKind"), "missing NodeKind: {names:?}");
        assert!(names.contains(&"new"), "missing new: {names:?}");
        assert!(names.contains(&"parse"), "missing parse: {names:?}");
        assert!(names.contains(&"Callback"), "missing Callback: {names:?}");
        assert!(names.contains(&"MAX_DEPTH"), "missing MAX_DEPTH: {names:?}");
        assert!(names.contains(&"utils"), "missing utils: {names:?}");
    }

    #[test]
    fn python_symbols_extracted() {
        let code = r#"
class MyParser:
    def __init__(self):
        pass

    async def parse(self, text):
        pass

def helper():
    pass
"#;
        let (syms, _refs) = extract_symbols("parser.py", code, "py");
        let names: Vec<&str> = syms.iter().map(|s| s.name.as_str()).collect();
        assert!(names.contains(&"MyParser"));
        assert!(names.contains(&"parse"));
        assert!(names.contains(&"helper"));
    }

    #[test]
    fn js_symbols_extracted() {
        let code = r#"
export function processData(data) {}
export class EventEmitter {}
const handleClick = (e) => {}
export async function fetchUser() {}
"#;
        let (syms, _refs) = extract_symbols("app.js", code, "js");
        let names: Vec<&str> = syms.iter().map(|s| s.name.as_str()).collect();
        assert!(names.contains(&"processData"));
        assert!(names.contains(&"EventEmitter"));
        assert!(names.contains(&"handleClick"));
        assert!(names.contains(&"fetchUser"));
    }

    #[test]
    fn ts_symbols_extracted() {
        let code = r#"
export interface Config {
    debug: boolean;
}
export type UserId = string;
export class Service {}
function helper() {}
"#;
        let (syms, _refs) = extract_symbols("service.ts", code, "ts");
        let names: Vec<&str> = syms.iter().map(|s| s.name.as_str()).collect();
        assert!(names.contains(&"Config"));
        assert!(names.contains(&"UserId"));
        assert!(names.contains(&"Service"));
        assert!(names.contains(&"helper"));
    }

    #[test]
    fn go_symbols_extracted() {
        let code = r#"
func NewParser() *Parser {
    return &Parser{}
}

func (p *Parser) Parse() error {
    return nil
}

type Config struct {
    Debug bool
}

type Handler interface {
    Handle()
}
"#;
        let (syms, _refs) = extract_symbols("parser.go", code, "go");
        let names: Vec<&str> = syms.iter().map(|s| s.name.as_str()).collect();
        assert!(names.contains(&"NewParser"));
        assert!(names.contains(&"Parse"));
        assert!(names.contains(&"Config"));
        assert!(names.contains(&"Handler"));
    }

    #[test]
    fn references_found() {
        let code = r#"
fn process(input: &str) -> ParseResult {
    let parser = Parser::new();
    let config = Config::default();
    parser.run(config)
}
"#;
        let (syms, refs) = extract_symbols("main.rs", code, "rs");
        let ref_names: Vec<&str> = refs.iter().map(|r| r.name.as_str()).collect();
        // Parser, Config, ParseResult should be references
        assert!(
            ref_names.contains(&"Parser"),
            "missing Parser ref: {ref_names:?}"
        );
        assert!(
            ref_names.contains(&"Config"),
            "missing Config ref: {ref_names:?}"
        );
        assert!(
            ref_names.contains(&"ParseResult"),
            "missing ParseResult ref: {ref_names:?}"
        );
        // 'process' is a definition, not a reference
        let def_names: Vec<&str> = syms.iter().map(|s| s.name.as_str()).collect();
        assert!(def_names.contains(&"process"));
    }

    #[test]
    fn line_numbers_correct() {
        let code = "fn first() {}\n\nfn second() {}\n";
        let (syms, _) = extract_symbols("test.rs", code, "rs");
        let first = syms.iter().find(|s| s.name == "first").unwrap();
        let second = syms.iter().find(|s| s.name == "second").unwrap();
        assert_eq!(first.line, 1);
        assert_eq!(second.line, 3);
    }

    #[test]
    fn fqn_format() {
        let sym = Symbol {
            kind: SymbolKind::Function,
            name: "parse".to_string(),
            file_path: "src/parser.rs".to_string(),
            line: 10,
        };
        assert_eq!(sym.fqn(), "src/parser.rs::parse");
    }

    #[test]
    fn empty_file_no_crash() {
        let (syms, refs) = extract_symbols("empty.rs", "", "rs");
        assert!(syms.is_empty());
        assert!(refs.is_empty());
    }

    #[test]
    fn unknown_language_returns_empty() {
        let (syms, refs) = extract_symbols("data.csv", "a,b,c", "csv");
        assert!(syms.is_empty());
        assert!(refs.is_empty());
    }

    #[test]
    fn regexes_compile() {
        let _ = &*RUST_FN_RE;
        let _ = &*RUST_STRUCT_RE;
        let _ = &*RUST_TRAIT_RE;
        let _ = &*RUST_ENUM_RE;
        let _ = &*RUST_IMPL_RE;
        let _ = &*RUST_TYPE_RE;
        let _ = &*RUST_CONST_RE;
        let _ = &*RUST_STATIC_RE;
        let _ = &*RUST_MOD_RE;
        let _ = &*PY_DEF_RE;
        let _ = &*PY_CLASS_RE;
        let _ = &*JS_FN_RE;
        let _ = &*JS_CLASS_RE;
        let _ = &*JS_CONST_FN_RE;
        let _ = &*TS_INTERFACE_RE;
        let _ = &*TS_TYPE_RE;
        let _ = &*GO_FUNC_RE;
        let _ = &*GO_METHOD_RE;
        let _ = &*GO_TYPE_RE;
        let _ = &*IDENT_RE;
    }
}
