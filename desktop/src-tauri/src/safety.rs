//! A shared project is someone else's code compiled on this Mac. Before anything
//! is built, the sources are checked here, and again by `shell/check-project.sh`
//! when the shell provides it. Both layers hold; neither replaces the other.
//!
//! Faust: no `ffunction`, `fconstant`, `fvariable` (foreign C), no `soundfile`
//! (reads files), no `component(...)` and no `import`/`library` of anything but a
//! standard `.lib` name.
//!
//! Logic module (plain C++): the only preprocessor line allowed is exactly
//! `#include "logic_api.h"`; no line continuations (a backslash at the end of a
//! line could hide a directive), no `[[` attributes, no casts that launder
//! pointers (`reinterpret_cast`, `const_cast`, C-style `(*`), and none of the
//! tokens that mean allocation, threads, files, processes, the network or inline
//! assembly. Comments are stripped with string and character literals respected,
//! so a `/*` inside a string cannot hide the code that follows.
//!
//! Interface (`ui.json`, data the shell compiles into the plugin as bytes): at most
//! 64 KB of UTF-8 holding one JSON object in the format of `src/lib/ui/schema.ts`
//! (`projectUiSchema`). The same rules as `shell/tools/check-ui-json.py`.

use serde_json::Value;
use std::fmt;
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Violation {
    pub file: String,
    pub line: usize,
    pub reason: String,
}

impl fmt::Display for Violation {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // A JSON file's problems are about a field, not a line: those carry line 0.
        if self.line == 0 {
            write!(f, "{}: {}", self.file, self.reason)
        } else {
            write!(f, "{}:{}: {}", self.file, self.line, self.reason)
        }
    }
}

const FAUST_FORBIDDEN: &[(&str, &str)] = &[
    ("ffunction", "calls foreign C code"),
    ("fconstant", "reads a foreign C constant"),
    ("fvariable", "reads a foreign C variable"),
    ("foreign", "reaches foreign code"),
    ("soundfile", "reads audio files from disk"),
    ("component", "pulls in another file"),
];

/// The `.lib` files Faust 2.88.0 ships, minus `soundfiles.lib` (it reads files). The same
/// list as `shell/check-project.sh`, so both layers agree.
const STANDARD_LIBS: &[&str] = &[
    "stdfaust.lib", "all.lib", "aanl.lib", "analyzers.lib", "basics.lib", "compressors.lib", "debug.lib",
    "delays.lib", "demos.lib", "doc.lib", "dx7.lib", "effect.lib", "env.lib", "envelopes.lib", "fds.lib",
    "filter.lib", "filters.lib", "hoa.lib", "hysteresis.lib", "instruments.lib", "interpolators.lib",
    "lfo.lib", "linearalgebra.lib", "math.lib", "maths.lib", "maxmsp.lib", "mi.lib", "misceffects.lib",
    "motion.lib", "music.lib", "noises.lib", "operator.lib", "oscillator.lib", "oscillators.lib",
    "phaflangers.lib", "physmodels.lib", "pitchenv.lib", "platform.lib", "quantizers.lib",
    "reducemaps.lib", "reverbs.lib", "routes.lib", "sf.lib", "signals.lib", "spats.lib", "synths.lib",
    "tonestacks.lib", "tubes.lib", "vaeffects.lib", "version.lib", "wdmodels.lib", "webaudio.lib",
];

/// Identifiers a logic module may not use, with the reason.
const LOGIC_FORBIDDEN: &[(&str, &str)] = &[
    ("asm", "inline assembly"),
    ("__asm", "inline assembly"),
    ("__asm__", "inline assembly"),
    ("new", "allocation"),
    ("delete", "allocation"),
    ("malloc", "allocation"),
    ("calloc", "allocation"),
    ("realloc", "allocation"),
    ("alloca", "allocation"),
    ("free", "allocation"),
    ("fopen", "files"),
    ("freopen", "files"),
    ("fread", "files"),
    ("fwrite", "files"),
    ("fclose", "files"),
    ("open", "files"),
    ("read", "files"),
    ("write", "files"),
    ("close", "files"),
    ("unlink", "files"),
    ("remove", "files"),
    ("rename", "files"),
    ("mmap", "memory mapping"),
    ("mprotect", "memory protection"),
    ("socket", "network"),
    ("connect", "network"),
    ("bind", "network"),
    ("listen", "network"),
    ("accept", "network"),
    ("send", "network"),
    ("recv", "network"),
    ("system", "runs a process"),
    ("popen", "runs a process"),
    ("pclose", "runs a process"),
    ("fork", "runs a process"),
    ("vfork", "runs a process"),
    ("posix_spawn", "runs a process"),
    ("syscall", "system call"),
    ("dlopen", "loads code"),
    ("dlsym", "loads code"),
    ("dlclose", "loads code"),
    ("thread", "threads"),
    ("std", "the standard library"),
    ("getenv", "environment"),
    ("setenv", "environment"),
    ("putenv", "environment"),
    ("extern", "external linkage"),
    ("setjmp", "non-local jumps"),
    ("longjmp", "non-local jumps"),
    ("signal", "signals"),
    ("raise", "signals"),
    ("abort", "ends the process"),
    ("exit", "ends the process"),
    ("_exit", "ends the process"),
    ("__attribute__", "compiler attributes"),
    // What could switch off the step budget in the plugin (decision 46): clang takes `__attribute`
    // without the trailing underscores too, and a pragma or `no_sanitize` can exempt a function;
    // the platform's own names reach the budget itself. build.sh also checks the compiled module.
    ("__attribute", "compiler attributes"),
    ("__declspec", "compiler attributes"),
    ("__pragma", "pragmas"),
    ("_Pragma", "pragmas"),
    ("no_sanitize", "turns off the step budget"),
    ("disable_sanitizer_instrumentation", "turns off the step budget"),
    ("no_instrument_function", "turns off the step budget"),
    ("pangea", "the platform's own names"),
    ("reinterpret_cast", "launders pointers"),
    ("const_cast", "launders pointers"),
    ("goto", "jumps"),
];

/// Prefixes that mean the same as the list above (`pthread_create`, `execve`, `__builtin_trap`).
const LOGIC_FORBIDDEN_PREFIXES: &[(&str, &str)] = &[
    ("pthread_", "threads"),
    ("exec", "runs a process"),
    ("__builtin_", "compiler built-ins"),
    ("__sync_", "atomics on raw memory"),
    ("__atomic_", "atomics on raw memory"),
    ("__sanitizer", "the step budget's own names"),
];

/// Only `"` opens a literal, in both languages: in Faust `'` is the delay operator, and in
/// a logic module an apostrophe is refused outright (no character literals, no digit
/// separators), so it can never be read two ways. The languages differ in their escape rule.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Lang {
    Faust,
    Cpp,
}

/// Replaces `//` and `/* */` comments with spaces, keeping newlines so line
/// numbers stay right. String (and, for C++, character) literals are copied through
/// untouched, so a comment opener inside a literal does not start a comment and
/// a literal inside a comment does not end it. Literals themselves are kept, so a
/// forbidden word inside a string is still flagged, which errs on the safe side.
/// A literal that reaches the end of its line is left open on purpose; `check_strings`
/// refuses such sources, because the compiler would read them differently.
fn strip_comments(src: &str, _lang: Lang) -> String {
    let b: Vec<char> = src.chars().collect();
    let mut out = String::with_capacity(src.len());
    let mut i = 0;
    while i < b.len() {
        let c = b[i];
        let next = b.get(i + 1).copied();
        if c == '/' && next == Some('/') {
            while i < b.len() && b[i] != '\n' {
                out.push(' ');
                i += 1;
            }
        } else if c == '/' && next == Some('*') {
            out.push_str("  ");
            i += 2;
            while i < b.len() && !(b[i] == '*' && b.get(i + 1) == Some(&'/')) {
                out.push(if b[i] == '\n' { '\n' } else { ' ' });
                i += 1;
            }
            if i < b.len() {
                out.push_str("  ");
                i += 2;
            }
        } else if c == '"' {
            // copy the literal through, honouring escapes; an unterminated one runs to the line end
            let quote = c;
            out.push(c);
            i += 1;
            while i < b.len() && b[i] != quote && b[i] != '\n' {
                if b[i] == '\\' && i + 1 < b.len() && b[i + 1] != '\n' {
                    out.push(b[i]);
                    i += 1;
                }
                out.push(b[i]);
                i += 1;
            }
            if i < b.len() && b[i] == quote {
                out.push(quote);
                i += 1;
            }
        } else {
            out.push(c);
            i += 1;
        }
    }
    out
}

/// Refuses any control character other than newline (0x0A) and tab (0x09), carriage
/// returns included, so a line end can never differ between this checker, the shell's
/// checker and the compiler.
fn check_control_chars(src: &str, file: &str) -> Vec<Violation> {
    let mut line = 1;
    for c in src.chars() {
        if c == '\n' {
            line += 1;
            continue;
        }
        if c.is_control() && c != '\t' {
            return vec![Violation {
                file: file.into(),
                line,
                reason: format!("control character U+{:04X} is not allowed (only newline and tab)", c as u32),
            }];
        }
    }
    Vec::new()
}

/// Refuses a string literal that is not closed on its own line (the compiler would read
/// the rest of the file differently from this checker). Comments are skipped first.
fn check_strings(src: &str, lang: Lang, file: &str) -> Vec<Violation> {
    let b: Vec<char> = src.chars().collect();
    let mut out = Vec::new();
    let mut i = 0;
    let mut line = 1;
    while i < b.len() {
        let c = b[i];
        let next = b.get(i + 1).copied();
        if c == '\n' {
            line += 1;
            i += 1;
        } else if c == '/' && next == Some('/') {
            while i < b.len() && b[i] != '\n' {
                i += 1;
            }
        } else if c == '/' && next == Some('*') {
            i += 2;
            while i < b.len() && !(b[i] == '*' && b.get(i + 1) == Some(&'/')) {
                if b[i] == '\n' {
                    line += 1;
                }
                i += 1;
            }
            i += 2;
        } else if c == '"' {
            let quote = c;
            let start = line;
            i += 1;
            let mut closed = false;
            while i < b.len() && b[i] != '\n' {
                // A comment marker inside a literal is refused in both languages, so the
                // scanner can never disagree with the compiler about where a comment is.
                if b[i] == '/' && (b.get(i + 1) == Some(&'/') || b.get(i + 1) == Some(&'*'))
                    || (b[i] == '*' && b.get(i + 1) == Some(&'/'))
                {
                    out.push(Violation {
                        file: file.into(),
                        line: start,
                        reason: "a comment marker (//, /* or */) inside a string literal is not allowed".into(),
                    });
                    return out;
                }
                if b[i] == '\\' {
                    match lang {
                        // Faust escape rules are not relied on at all: no backslash in a string.
                        Lang::Faust => {
                            out.push(Violation {
                                file: file.into(),
                                line: start,
                                reason: "a backslash inside a string literal is not allowed".into(),
                            });
                            return out;
                        }
                        // C++ escapes are well defined: skip the escaped character.
                        Lang::Cpp => {
                            if i + 1 < b.len() && b[i + 1] != '\n' {
                                i += 2;
                                continue;
                            }
                        }
                    }
                }
                if b[i] == quote {
                    closed = true;
                    i += 1;
                    break;
                }
                i += 1;
            }
            if !closed {
                out.push(Violation {
                    file: file.into(),
                    line: start,
                    reason: "a string literal must open and close on the same line (this one spans a line or is not closed)".into(),
                });
                return out;
            }
        } else {
            i += 1;
        }
    }
    out
}

/// Every `import` or `library` token must be followed, with any whitespace between
/// (newlines included; comments were stripped first), by `(`, a string naming a
/// standard library, and `)`. Anything else is refused.
fn check_faust_imports(text: &str) -> Vec<Violation> {
    let b: Vec<char> = text.chars().collect();
    let mut out = Vec::new();
    let mut i = 0;
    let mut line = 1;
    while i < b.len() {
        if b[i] == '\n' {
            line += 1;
            i += 1;
            continue;
        }
        if !is_ident_char(b[i]) || (i > 0 && is_ident_char(b[i - 1])) {
            i += 1;
            continue;
        }
        let start = i;
        while i < b.len() && is_ident_char(b[i]) {
            i += 1;
        }
        let word: String = b[start..i].iter().collect();
        if word != "import" && word != "library" {
            continue;
        }
        let at = line;
        let mut j = i;
        let mut skip_ws = |j: &mut usize| {
            while *j < b.len() && b[*j].is_whitespace() {
                if b[*j] == '\n' {
                    line += 1;
                }
                *j += 1;
            }
        };
        skip_ws(&mut j);
        let mut ok = false;
        if j < b.len() && b[j] == '(' {
            j += 1;
            skip_ws(&mut j);
            if j < b.len() && b[j] == '"' {
                let s0 = j + 1;
                let mut k = s0;
                while k < b.len() && b[k] != '"' && b[k] != '\n' {
                    k += 1;
                }
                if k < b.len() && b[k] == '"' {
                    let name: String = b[s0..k].iter().collect();
                    j = k + 1;
                    skip_ws(&mut j);
                    if j < b.len() && b[j] == ')' && STANDARD_LIBS.contains(&name.as_str()) {
                        ok = true;
                        j += 1;
                    } else if j < b.len() && b[j] == ')' {
                        out.push(Violation {
                            file: "dsp.dsp".into(),
                            line: at,
                            reason: format!("`{word}(\"{name}\")` is not allowed: only the standard Faust libraries may be used"),
                        });
                        i = j + 1;
                        continue;
                    }
                }
            }
        }
        if !ok {
            out.push(Violation {
                file: "dsp.dsp".into(),
                line: at,
                reason: format!("`{word}` must be written as {word}(\"name.lib\") with a standard library name"),
            });
        }
        i = j.max(i);
    }
    out
}

fn is_ident_char(c: char) -> bool {
    c.is_ascii_alphanumeric() || c == '_'
}

/// Yields (line number, identifier) for every identifier in the text.
/// The lines where a `[` outside a string is followed, after nothing but blank, by another `[`.
fn attribute_brackets(text: &str) -> Vec<usize> {
    let chars: Vec<char> = text.chars().collect();
    let mut lines = Vec::new();
    let (mut line, mut in_string, mut i) = (1usize, false, 0usize);
    while i < chars.len() {
        let c = chars[i];
        if c == '\n' {
            line += 1;
        } else if in_string {
            if c == '\\' {
                i += 1;
            } else if c == '"' {
                in_string = false;
            }
        } else if c == '"' {
            in_string = true;
        } else if c == '[' {
            let mut j = i + 1;
            while j < chars.len() && chars[j].is_whitespace() {
                j += 1;
            }
            if j < chars.len() && chars[j] == '[' && lines.last() != Some(&line) {
                lines.push(line);
            }
        }
        i += 1;
    }
    lines
}

fn identifiers(text: &str) -> impl Iterator<Item = (usize, &str)> {
    text.lines().enumerate().flat_map(|(i, line)| {
        let mut out = Vec::new();
        let mut start: Option<usize> = None;
        for (j, c) in line.char_indices() {
            match (start, is_ident_char(c)) {
                (None, true) => start = Some(j),
                (Some(s), false) => {
                    out.push((i + 1, &line[s..j]));
                    start = None;
                }
                _ => {}
            }
        }
        if let Some(s) = start {
            out.push((i + 1, &line[s..]));
        }
        out.into_iter().filter(|(_, w)| !w.chars().next().is_some_and(|c| c.is_ascii_digit()))
    })
}

/// Markers of Faust documentation sections, which the Faust lexer treats specially.
const FAUST_DOC_MARKERS: &[&str] = &["<mdoc", "</mdoc>", "<equation", "<diagram", "<metadata", "<notice", "<listing"];

/// Refuses documentation sections anywhere in the raw text (before stripping), since
/// what sits inside them is lexed by different rules.
fn check_faust_doc_markers(src: &str) -> Vec<Violation> {
    let lower = src.to_ascii_lowercase();
    for (i, l) in lower.lines().enumerate() {
        if let Some(m) = FAUST_DOC_MARKERS.iter().find(|m| l.contains(*m)) {
            return vec![Violation {
                file: "dsp.dsp".into(),
                line: i + 1,
                reason: format!("`{m}` is not allowed: documentation sections are off limits"),
            }];
        }
    }
    Vec::new()
}

pub fn check_faust(src: &str) -> Vec<Violation> {
    let control = check_control_chars(src, "dsp.dsp");
    if !control.is_empty() {
        return control;
    }
    let docs = check_faust_doc_markers(src);
    if !docs.is_empty() {
        return docs;
    }
    let strings = check_strings(src, Lang::Faust, "dsp.dsp");
    if !strings.is_empty() {
        return strings;
    }
    let text = strip_comments(src, Lang::Faust);
    let mut out = Vec::new();
    for (line, word) in identifiers(&text) {
        if let Some((_, why)) = FAUST_FORBIDDEN.iter().find(|(w, _)| *w == word) {
            out.push(Violation { file: "dsp.dsp".into(), line, reason: format!("`{word}` is not allowed: it {why}") });
        }
    }
    out.extend(check_faust_imports(&text));
    out.sort_by_key(|v| v.line);
    out
}

/// Both sources, in memory, before anything is written to disk.
pub fn check_sources(faust: &str, logic: Option<&str>) -> Vec<Violation> {
    let mut out = check_faust(faust);
    if let Some(l) = logic {
        out.extend(check_logic(l));
    }
    out
}

pub fn check_logic(src: &str) -> Vec<Violation> {
    let mut out = Vec::new();
    let logic = |line: usize, reason: String| Violation { file: "logic.cpp".into(), line, reason };

    // 0. Control characters other than newline and tab, before anything else.
    let control = check_control_chars(src, "logic.cpp");
    if !control.is_empty() {
        return control;
    }

    // 1. Line continuations are refused outright: `#inc\` + newline + `lude` is one
    //    directive to the compiler but two harmless lines to a naive reader. So are raw
    //    string literals, digraphs and trigraphs, which could spell what the check does not see.
    for (i, l) in src.lines().enumerate() {
        if l.trim_end_matches(['\r', ' ', '\t']).ends_with('\\') {
            out.push(logic(i + 1, "a line may not end with a backslash (line continuation)".into()));
        }
        if l.contains("R\"") {
            out.push(logic(i + 1, "raw string literals (R\"...\") are not allowed".into()));
        }
        if l.contains("%:") || l.contains("<:") || l.contains(":>") || l.contains("<%") || l.contains("%>") {
            out.push(logic(i + 1, "digraphs (%:, <:, :>, <%, %>) are not allowed".into()));
        }
        if l.contains("??") {
            out.push(logic(i + 1, "trigraphs (??) are not allowed".into()));
        }
    }
    if !out.is_empty() {
        return out;
    }
    let strings = check_strings(src, Lang::Cpp, "logic.cpp");
    if !strings.is_empty() {
        return strings;
    }

    let text = strip_comments(src, Lang::Cpp);

    // 2. An apostrophe outside a comment (a character literal, or a C++14 digit separator
    //    such as 1'000) is refused, so the lexer never has to decide what it means.
    for (i, l) in text.lines().enumerate() {
        if l.contains('\'') {
            out.push(logic(i + 1, "an apostrophe is not allowed outside comments (no character literals, no digit separators)".into()));
        }
    }

    // 3. Preprocessor: exactly one directive is allowed, `#include "logic_api.h"`.
    for (i, l) in text.lines().enumerate() {
        let t = l.trim();
        if let Some(rest) = t.strip_prefix('#') {
            let directive = rest.trim();
            let allowed = directive
                .strip_prefix("include")
                .map(|inc| inc.trim() == "\"logic_api.h\"")
                .unwrap_or(false);
            if !allowed {
                out.push(logic(i + 1, format!("`#{directive}` is not allowed: the only preprocessor line permitted is #include \"logic_api.h\"")));
            }
        }
    }

    // 4. Attributes and pointer-laundering casts. `[` then `[` is an attribute whatever blank lies
    //    between them, spaces, a newline or a comment (already spaces here); digraphs are refused above.
    for line in attribute_brackets(&text) {
        out.push(logic(line, "`[[` attributes are not allowed".into()));
    }
    for (i, l) in text.lines().enumerate() {
        if let Some(pos) = l.find("(*") {
            // `(*` starts a C-style cast to a pointer type or a function-pointer declarator; both are refused.
            let _ = pos;
            out.push(logic(i + 1, "`(*` is not allowed: no C-style pointer casts or function pointers".into()));
        }
    }

    // 5. Forbidden identifiers.
    for (line, word) in identifiers(&text) {
        if let Some((_, why)) = LOGIC_FORBIDDEN.iter().find(|(w, _)| *w == word) {
            out.push(logic(line, format!("`{word}` is not allowed: {why}")));
        } else if let Some((_, why)) = LOGIC_FORBIDDEN_PREFIXES.iter().find(|(p, _)| word.starts_with(p)) {
            out.push(logic(line, format!("`{word}` is not allowed: {why}")));
        }
    }
    out.sort_by_key(|v| v.line);
    out
}

/// Checks the files the desktop app wrote into a project directory.
pub fn check_project_dir(dir: &Path) -> anyhow::Result<Vec<Violation>> {
    let mut out = Vec::new();
    let dsp = std::fs::read_to_string(dir.join("dsp.dsp"))?;
    out.extend(check_faust(&dsp));
    let logic = dir.join("logic.cpp");
    if logic.is_file() {
        out.extend(check_logic(&std::fs::read_to_string(logic)?));
    }
    out.extend(check_ui_file(&dir.join("ui.json"))?);
    Ok(out)
}

// ---- The interface: ui.json ---------------------------------------------------------

pub const UI_MAX_BYTES: usize = 64 * 1024;
const UI_STYLES: &[&str] = &["vintage", "minimal", "pedal", "playful", "retro"];
const UI_SIZES: &[&str] = &["s", "m", "l"];
const UI_KNOB_STYLES: &[&str] = &["round", "arc"];
const LAYOUT_WIDTH: f64 = 720.0;
const LAYOUT_MIN_HEIGHT: f64 = 120.0;
const LAYOUT_MAX_HEIGHT: f64 = 1600.0;
const LAYOUT_MAX_ITEMS: usize = 128;
/// What JavaScript's `String.prototype.trim` removes, which is what zod's `.trim()` does.
const JS_SPACE: &[char] = &[
    '\t', '\n', '\u{0B}', '\u{0C}', '\r', ' ', '\u{A0}', '\u{1680}', '\u{2000}', '\u{2001}', '\u{2002}', '\u{2003}',
    '\u{2004}', '\u{2005}', '\u{2006}', '\u{2007}', '\u{2008}', '\u{2009}', '\u{200A}', '\u{2028}', '\u{2029}',
    '\u{202F}', '\u{205F}', '\u{3000}', '\u{FEFF}',
];

/// The text the app writes as `ui.json` for a version's interface, or `None` when the
/// version carries none (null or an empty object): then no `ui.json` is written and the
/// plugin is drawn with the default look.
pub fn ui_json_text(ui: &Value) -> Option<String> {
    match ui {
        Value::Null => None,
        Value::Object(m) if m.is_empty() => None,
        other => serde_json::to_string_pretty(other).ok(),
    }
}

/// Checks `ui.json` as it will be written: its size, that it is JSON, then the format.
pub fn check_ui_json(text: &str) -> Vec<Violation> {
    if text.len() > UI_MAX_BYTES {
        return vec![ui_violation("the file is larger than 64 KB".into())];
    }
    match serde_json::from_str::<Value>(text) {
        Ok(ui) => check_ui(&ui),
        Err(e) => vec![ui_violation(format!("the file is not valid JSON: {e}"))],
    }
}

/// Checks a `ui.json` found in a project directory; nothing to check when there is none.
fn check_ui_file(path: &Path) -> anyhow::Result<Vec<Violation>> {
    let meta = match std::fs::symlink_metadata(path) {
        Ok(m) => m,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(e) => return Err(e.into()),
    };
    if !meta.file_type().is_file() {
        return Ok(vec![ui_violation("the file must be a plain file, not a link or a directory".into())]);
    }
    if meta.len() > UI_MAX_BYTES as u64 {
        return Ok(vec![ui_violation("the file is larger than 64 KB".into())]);
    }
    match String::from_utf8(std::fs::read(path)?) {
        Ok(text) => Ok(check_ui_json(&text)),
        Err(_) => Ok(vec![ui_violation("the file is not UTF-8 text".into())]),
    }
}

/// Checks a parsed interface against the format. Unknown fields are allowed, as zod allows them.
pub fn check_ui(ui: &Value) -> Vec<Violation> {
    let Some(o) = ui.as_object() else {
        return vec![ui_violation(format!("the file must hold one JSON object (got {})", shown(ui)))];
    };
    let mut p: Vec<String> = Vec::new();

    match o.get("title") {
        None => p.push("title is missing".into()),
        Some(v) => check_text(&mut p, "title", v, 1, 60, true),
    }

    match o.get("palette") {
        None => p.push("palette is missing".into()),
        Some(Value::Object(palette)) => {
            for name in ["bg", "panel", "accent", "text"] {
                match palette.get(name) {
                    None => p.push(format!("palette.{name} is missing")),
                    Some(v) if !is_colour(v) => p.push(format!("palette.{name} must be a colour as #rrggbb (got {})", shown(v))),
                    Some(_) => {}
                }
            }
        }
        Some(v) => p.push(format!("palette must be an object with bg, panel, accent and text (got {})", shown(v))),
    }

    match o.get("knobStyle") {
        None => p.push("knobStyle is missing".into()),
        Some(v) if !one_of(v, UI_KNOB_STYLES) => p.push(format!("knobStyle must be round or arc (got {})", shown(v))),
        Some(_) => {}
    }

    if let Some(v) = o.get("style") {
        if !one_of(v, UI_STYLES) {
            p.push(format!("style must be one of {} (got {})", UI_STYLES.join(", "), shown(v)));
        }
    }

    if let Some(layout) = o.get("layout") {
        check_layout(&mut p, layout);
    }
    p.into_iter().map(ui_violation).collect()
}

fn check_layout(p: &mut Vec<String>, layout: &Value) {
    let Some(l) = layout.as_object() else {
        p.push(format!("layout must be an object with width, height and items (got {})", shown(layout)));
        return;
    };
    let got = |key: &str| l.get(key).map(shown).unwrap_or_else(|| "nothing".into());
    if !l.get("width").and_then(Value::as_f64).is_some_and(|w| w == LAYOUT_WIDTH) {
        p.push(format!("layout.width must be 720 (got {})", got("width")));
    }
    if !is_whole(l.get("height"), LAYOUT_MIN_HEIGHT, LAYOUT_MAX_HEIGHT) {
        p.push(format!("layout.height must be a whole number from 120 to 1600 (got {})", got("height")));
    }
    let Some(items) = l.get("items").and_then(Value::as_array) else {
        p.push(format!("layout.items must be a list (got {})", got("items")));
        return;
    };
    if items.len() > LAYOUT_MAX_ITEMS {
        p.push(format!("layout.items may hold at most 128 items (it has {})", items.len()));
        return;
    }
    for (i, item) in items.iter().enumerate() {
        let at = format!("layout.items[{i}]");
        let Some(it) = item.as_object() else {
            p.push(format!("{at} must be an object (got {})", shown(item)));
            continue;
        };
        let got = |key: &str| it.get(key).map(shown).unwrap_or_else(|| "nothing".into());
        match it.get("key") {
            None => p.push(format!("{at}.key is missing")),
            Some(v) => check_text(p, &format!("{at}.key"), v, 1, 200, false),
        }
        if !is_whole(it.get("x"), 0.0, LAYOUT_WIDTH) {
            p.push(format!("{at}.x must be a whole number from 0 to 720 (got {})", got("x")));
        }
        if !is_whole(it.get("y"), 0.0, LAYOUT_MAX_HEIGHT) {
            p.push(format!("{at}.y must be a whole number from 0 to 1600 (got {})", got("y")));
        }
        if !it.get("size").is_some_and(|v| one_of(v, UI_SIZES)) {
            p.push(format!("{at}.size must be s, m or l (got {})", got("size")));
        }
        if let Some(v) = it.get("label") {
            check_text(p, &format!("{at}.label"), v, 1, 40, true);
        }
        if let Some(v) = it.get("hidden") {
            if !v.is_boolean() {
                p.push(format!("{at}.hidden must be true or false (got {})", shown(v)));
            }
        }
    }
}

fn ui_violation(reason: String) -> Violation {
    Violation { file: "ui.json".into(), line: 0, reason }
}

/// Text whose length, after trimming when asked, is within the limits. Measured as zod 4
/// measures it: in Unicode code points, not UTF-16 units.
fn check_text(p: &mut Vec<String>, at: &str, v: &Value, lo: usize, hi: usize, trim: bool) {
    let Some(s) = v.as_str() else {
        p.push(format!("{at} must be text (got {})", shown(v)));
        return;
    };
    let n = if trim { s.trim_matches(JS_SPACE) } else { s }.chars().count();
    if n < lo || n > hi {
        let after = if trim { " after trimming spaces" } else { "" };
        p.push(format!("{at} must be {lo} to {hi} characters{after} (it has {n})"));
    }
}

/// A whole number within the limits: an integer, or a number with nothing after the point.
fn is_whole(v: Option<&Value>, lo: f64, hi: f64) -> bool {
    v.and_then(Value::as_f64).is_some_and(|n| n.is_finite() && n.fract() == 0.0 && n >= lo && n <= hi)
}

fn is_colour(v: &Value) -> bool {
    v.as_str().is_some_and(|s| s.len() == 7 && s.starts_with('#') && s[1..].bytes().all(|b| b.is_ascii_hexdigit()))
}

fn one_of(v: &Value, choices: &[&str]) -> bool {
    v.as_str().is_some_and(|s| choices.contains(&s))
}

/// A short rendering of a value for a message.
fn shown(v: &Value) -> String {
    let text = serde_json::to_string(v).unwrap_or_default();
    if text.chars().count() <= 40 {
        text
    } else {
        format!("{}...", text.chars().take(40).collect::<String>())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const GOOD_FAUST: &str = r#"import("stdfaust.lib");
declare name "Warm Tape Delay";
// a soundfile mentioned in a comment is fine
time = hslider("Time [unit:ms][style:knob]", 350, 20, 1200, 1) : si.smoo;
process = _, _;
"#;

    #[test]
    fn clean_faust_passes() {
        assert!(check_faust(GOOD_FAUST).is_empty());
    }

    // The house smoother Pangea's plugins now use (decision 54):
    // a delay apostrophe, `with`, `~` and partial application, all plain Faust.
    #[test]
    fn the_house_smoother_passes() {
        let src = r#"import("stdfaust.lib");
smoo = smooth_for(0.02);
smooth_for(t, x) = x - ((x - x') * 1' : (+ : *(ba.tau2pole(t)) : tiny) ~ _) with { tiny(e) = e * (abs(e) > 1e-30); };
time = hslider("Time [unit:ms][style:knob]", 350, 20, 1200, 1) : smoo;
process = _, _;
"#;
        assert!(check_faust(src).is_empty());
    }

    #[test]
    fn foreign_functions_and_soundfiles_are_refused() {
        let src = "import(\"stdfaust.lib\");\nfoo = ffunction(float foo(float), \"foo.h\", \"\");\nprocess = soundfile(\"x\", 2);\n";
        let v = check_faust(src);
        assert_eq!(v.len(), 2);
        assert_eq!(v[0].line, 2);
        assert!(v[0].reason.contains("ffunction"));
        assert_eq!(v[1].line, 3);
        assert!(v[1].reason.contains("soundfile"));
        assert!(!check_faust("process = fconstant(int x, <y.h>);").is_empty());
        assert!(!check_faust("process = fvariable(int x, <y.h>);").is_empty());
    }

    #[test]
    fn non_standard_imports_are_refused() {
        assert!(check_faust("import(\"stdfaust.lib\"); process = _;").is_empty());
        assert!(check_faust("fx = library(\"filters.lib\"); process = _;").is_empty());
        assert!(check_faust("import ( \"stdfaust.lib\" ); process = _;").is_empty());
        assert!(!check_faust("import(\"../secret.dsp\"); process = _;").is_empty());
        assert!(!check_faust("x = library(\"/etc/passwd\"); process = _;").is_empty());
        assert!(!check_faust("import(\"soundfiles.lib\"); process = _;").is_empty());
        assert!(!check_faust("import(\"mylib.lib\"); process = _;").is_empty());
        assert!(!check_faust("process = component(\"other.dsp\");").is_empty());
        // the word inside a block comment is ignored
        assert!(check_faust("/* component */ process = _;").is_empty());
    }

    #[test]
    fn imports_split_over_lines_or_comments_are_still_checked() {
        let v = check_faust("import\n(\"foo.lib\");\nprocess = _;");
        assert_eq!(v.len(), 1, "{v:?}");
        assert!(v[0].reason.contains("foo.lib"));
        assert!(!check_faust("import /* c */ (\"foo.lib\"); process = _;").is_empty());
        assert!(check_faust("import\n  (\"stdfaust.lib\");\nprocess = _;").is_empty());
        // an import that is not a plain call is refused too
        assert!(!check_faust("x = import; process = _;").is_empty());
        assert!(!check_faust("import(stdfaust); process = _;").is_empty());
    }

    #[test]
    fn multi_line_and_unterminated_strings_are_refused() {
        let v = check_faust("declare name \"Warm\nTape\";\nprocess = _;");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].line, 1);
        assert!(v[0].reason.contains("same line"));
        assert!(!check_faust("declare name \"open;\nprocess = _;").is_empty());
        // a delay operator is not a quote in Faust
        assert!(check_faust("x = _ : _'; declare s \"a b\"; process = x;").is_empty());
        // the same rule for a logic module
        assert!(!check_logic("#include \"logic_api.h\"\nconst char* s = \"ab\ncd\";").is_empty());
    }

    #[test]
    fn backslashes_and_comment_markers_inside_strings_are_refused() {
        // a backslash before a quote in Faust: refused outright, whatever Faust's escape rules are
        let v = check_faust("declare s \"a \\\" b\"; process = _;");
        assert_eq!(v.len(), 1, "{v:?}");
        assert!(v[0].reason.contains("backslash"));
        assert!(!check_faust("declare s \"a\\nb\"; process = _;").is_empty());
        // comment markers inside a string, both languages
        let v = check_faust("declare s \"a /* b\"; process = _;");
        assert!(v[0].reason.contains("comment marker"), "{v:?}");
        assert!(!check_faust("declare s \"a // b\"; process = _;").is_empty());
        assert!(!check_faust("declare s \"a */ b\"; process = _;").is_empty());
        assert!(!check_logic("#include \"logic_api.h\"\nconst char* s = \"x /* y\";").is_empty());
        assert!(!check_logic("#include \"logic_api.h\"\nconst char* s = \"x // y\";").is_empty());
        assert!(!check_logic("#include \"logic_api.h\"\nconst char* s = \"*/\";").is_empty());
        // C++ escapes stay allowed, and a lone slash or star is fine in both
        assert!(check_logic("#include \"logic_api.h\"\nconst char* s = \"a \\\" b / c * d\";").is_empty());
        assert!(check_faust("declare s \"a / b * c\"; process = _;").is_empty());
    }

    #[test]
    fn apostrophes_outside_comments_are_refused_in_logic() {
        let v = check_logic("#include \"logic_api.h\"\nint n = 1'000;");
        assert_eq!(v.len(), 1, "{v:?}");
        assert_eq!(v[0].line, 2);
        assert!(v[0].reason.contains("apostrophe"));
        assert!(!check_logic("#include \"logic_api.h\"\nchar c = 'x';").is_empty());
        assert!(!check_logic("#include \"logic_api.h\"\nconst char* s = \"it's\";").is_empty());
        // inside a comment it is fine
        assert!(check_logic("#include \"logic_api.h\"\n// it's fine /* isn't it */\nint n = 1000;").is_empty());
    }

    #[test]
    fn control_characters_are_refused_first() {
        // carriage returns, in either source
        let v = check_faust("import(\"stdfaust.lib\");\r\nprocess = _;\r\n");
        assert_eq!(v.len(), 1, "{v:?}");
        assert!(v[0].reason.contains("U+000D"));
        let v = check_logic("#include \"logic_api.h\"\r\nint n = 1;\r\n");
        assert_eq!(v.len(), 1, "{v:?}");
        assert_eq!(v[0].line, 1);
        // other controls too, and before any other rule (a \x0c with a forbidden word reports the control character)
        let v = check_faust("process = _;\x0cfoo = ffunction(float f(float), \"x\", \"\");");
        assert_eq!(v.len(), 1);
        assert!(v[0].reason.contains("U+000C"));
        assert!(!check_logic("#include \"logic_api.h\"\nint n = 1;\x00").is_empty());
        // tabs and newlines are fine, and a normal file still passes
        assert!(check_faust("import(\"stdfaust.lib\");\n\tprocess = _;\n").is_empty());
        assert!(check_faust(GOOD_FAUST).is_empty());
        assert!(check_logic(GOOD_LOGIC).is_empty(), "{:?}", check_logic(GOOD_LOGIC));
    }

    #[test]
    fn documentation_sections_are_refused() {
        let hidden = "import(\"stdfaust.lib\");\n<mdoc>\nfoo = ffunction(float f(float), \"x.h\", \"\");\n</mdoc>\nprocess = _;";
        let v = check_faust(hidden);
        assert_eq!(v.len(), 1, "{v:?}");
        assert_eq!(v[0].line, 2);
        assert!(v[0].reason.contains("<mdoc"));
        for m in ["<equation>", "<diagram>", "<metadata>", "<notice>", "<listing>", "</mdoc>", "<MDOC>"] {
            assert!(!check_faust(&format!("process = _; {m}")).is_empty(), "should refuse {m}");
        }
    }

    const GOOD_LOGIC: &str = r#"#include "logic_api.h"
// a tiny arpeggiator
struct Logic {
    int step;
    static const LogicParam* params(int* count) { *count = 0; return nullptr; }
    int process(const LogicBlock& b, LogicEvent* events, int maxEvents) {
        (void)b; (void)events; (void)maxEvents;
        const char* label = "step one, then two";
        (void)label;
        return 0;
    }
};
"#;

    #[test]
    fn clean_logic_passes() {
        assert!(check_logic(GOOD_LOGIC).is_empty(), "{:?}", check_logic(GOOD_LOGIC));
    }

    /// The MIDI tools fixture (logic API version 2: note events, display hints in `meta`, the
    /// platform's maths and LogicRandom) and every other example module pass in full, as does
    /// each one's silent or sounding Faust. Two Hands is checked when it is there; it is being
    /// written in parallel (docs/midi-tools.md).
    #[test]
    fn the_example_projects_pass() {
        assert!(check_sources(include_str!("../../../shell/examples/midi-steps/dsp.dsp"),
            Some(include_str!("../../../shell/examples/midi-steps/logic.cpp"))).is_empty(),
            "{:?}", check_sources(include_str!("../../../shell/examples/midi-steps/dsp.dsp"),
            Some(include_str!("../../../shell/examples/midi-steps/logic.cpp"))));
        let acid = check_sources(include_str!("../../../shell/examples/acid-line/dsp.dsp"),
            Some(include_str!("../../../shell/examples/acid-line/logic.cpp")));
        assert!(acid.is_empty(), "{acid:?}");
        let examples = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../shell/examples/two-hands");
        if let (Ok(dsp), Ok(logic)) = (std::fs::read_to_string(examples.join("dsp.dsp")), std::fs::read_to_string(examples.join("logic.cpp"))) {
            let v = check_sources(&dsp, Some(&logic));
            assert!(v.is_empty(), "two-hands: {v:?}");
        }
    }

    /// What could switch off the step budget, or reach it, is refused (decision 46).
    #[test]
    fn what_could_switch_off_the_step_budget_is_refused() {
        let base = GOOD_LOGIC;
        for (extra, word) in [
            ("__attribute((no_sanitize(\"coverage\"))) static int a() { return 1; }", "__attribute"),
            ("__attribute__((no_sanitize(\"coverage\"))) static int b() { return 1; }", "__attribute__"),
            ("_Pragma(\"clang optimize off\") static int c() { return 1; }", "_Pragma"),
            ("__declspec(noinline) static int d() { return 1; }", "__declspec"),
            ("namespace pangea { namespace logic { int e(); } }", "pangea"),
            ("static int f() { return __sanitizer_cov_trace_pc_guard_init(0, 0), 1; }", "__sanitizer_cov_trace_pc_guard_init"),
            ("[ [clang::no_sanitize(\"coverage\")] ] static int g() { return 1; }", "[["),
            ("[/* a comment */\n[clang::no_sanitize(\"coverage\")]] static int h() { return 1; }", "[["),
            ("__pragma(clang optimize off) static int j() { return 1; }", "__pragma"),
            ("static int disable_sanitizer_instrumentation = 1;", "disable_sanitizer_instrumentation"),
        ] {
            let v = check_logic(&format!("{base}\n{extra}\n"));
            assert!(v.iter().any(|x| x.reason.contains(&format!("`{word}`"))), "{extra}: {v:?}");
        }
        // Digraphs are refused outright, so <:<: cannot spell an attribute either.
        let v = check_logic(&format!("{base}\n<:<:clang::no_sanitize(\"coverage\"):>:> static int i() {{ return 1; }}\n"));
        assert!(v.iter().any(|x| x.reason.contains("digraphs")), "{v:?}");
        // Indexing and a meta string with brackets are not attributes.
        let ok = format!("{base}\nstatic int grid[4][4];\nstatic int at() {{ return grid[grid[0][1]][2]; }}\nstatic const char* m = \"[ [x]\";\n");
        assert!(check_logic(&ok).is_empty(), "{:?}", check_logic(&ok));
    }

    /// What a version 2 module writes, line by line, must not trip the checker.
    #[test]
    fn version_2_constructs_pass() {
        let v2 = r#"#include "logic_api.h"
static const char* const kLogicControls[1] = { "" };
static const int kNumLogicControls = 0;
static const LogicParam kLogicParams[] = {
    { "Rate", 0.0f, 4.0f, 1.0f, 1.0f, "[style:menu{1/32;1/16;1/8;dotted 1/8;triplet 1/8}]" },
    { "Accent 1", 0.0f, 127.0f, 68.0f, 1.0f, "[style:bars][group:Left hand][playhead:0]" },
    { "Gap", 0.0f, 100.0f, 10.0f, 1.0f, "[unit:%][style:knob]" },
    { "Variation", 0.0f, 9999.0f, 0.0f, 1.0f, "[style:generate]" },
};
static const int kNumLogicParams = 4;
struct Logic
{
    int process (const LogicBlock& b, LogicEvent* out, int maxEvents)
    {
        LogicRandom random (42ull);
        const double bar = b.ppqStart / b.beatsPerBar;
        const long long step = logic_floor (logic_wrap (bar, 1.0) * 16.0);
        const int velocity = logic_clamp ((int) logic_round (logic_pow (2.0, logic_sin (bar)) * 40.0), 1, 127);
        int count = 0;
        if (maxEvents > 2 && b.playing)
        {
            out[count++] = { 0, LOGIC_NOTE_OFF, 0.0f, 60, 1 };
            out[count++] = { 0, LOGIC_DISPLAY, (float) logic_mod (step, 4), 0, 0 };
            out[count++] = { 0, LOGIC_NOTE_ON, (float) velocity, 60 + random.below (12), 2 };
        }
        return count;
    }
    void prepare (double) {}
};
"#;
        assert!(check_logic(v2).is_empty(), "{:?}", check_logic(v2));
    }

    #[test]
    fn other_includes_and_directives_are_refused() {
        let v = check_logic("#include <cstdio>\n#include \"logic_api.h\"\n");
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].line, 1);
        assert!(check_logic("#  include   \"logic_api.h\"\n").is_empty());
        for bad in ["#define X 1", "#pragma once", "#if 1\n#endif", "#include <logic_api.h>", "#include \"logic_api.h\" extra"] {
            assert!(!check_logic(bad).is_empty(), "should refuse: {bad}");
        }
        // a trailing comment is stripped first, so it is fine
        assert!(check_logic("#include \"logic_api.h\" // the only include\n").is_empty());
    }

    #[test]
    fn line_continuations_cannot_hide_a_directive() {
        let hidden = "#inc\\\nlude <cstdio>\nint x;\n";
        let v = check_logic(hidden);
        assert!(!v.is_empty());
        assert!(v[0].reason.contains("backslash"));
        assert_eq!(v[0].line, 1);
        // a backslash followed by spaces still counts
        assert!(!check_logic("int x; \\   \nint y;").is_empty());
    }

    #[test]
    fn raw_strings_digraphs_and_trigraphs_are_refused() {
        for bad in ["const char* s = R\"(x)\";", "%:include \"logic_api.h\"", "int a<:3:>;", "int x = 1; ??=define X"] {
            assert!(!check_logic(bad).is_empty(), "should refuse: {bad}");
        }
        assert!(!check_logic("goto out;").is_empty());
        assert!(!check_faust("process = foreign(x);").is_empty());
    }

    #[test]
    fn a_comment_opener_inside_a_string_does_not_hide_code() {
        // a comment opener inside a string is refused outright, so it can hide nothing
        let src = "#include \"logic_api.h\"\nconst char* s = \"/*\";\nvoid* p = malloc(4);\nconst char* t = \"*/\";\n";
        let v = check_logic(src);
        assert_eq!(v.len(), 1, "{v:?}");
        assert_eq!(v[0].line, 2);
        assert!(v[0].reason.contains("comment marker"));
        // and a quote inside a comment does not start a string that swallows the rest
        let src2 = "#include \"logic_api.h\"\n// it's fine\nvoid* p = malloc(4);\n";
        assert!(check_logic(src2).iter().any(|x| x.reason.contains("malloc")));
    }

    #[test]
    fn attributes_and_casts_are_refused() {
        for bad in [
            "[[gnu::constructor]] void f() {}",
            "int* p = reinterpret_cast<int*>(0x1000);",
            "int* p = const_cast<int*>(q);",
            "void (*fp)() = 0;",
            "int* p = (int*)0x1000;".replace("(int*)", "(*)").as_str(),
        ] {
            assert!(!check_logic(bad).is_empty(), "should refuse: {bad}");
        }
    }

    #[test]
    fn dangerous_tokens_are_refused() {
        for bad in [
            "int* p = new int[4];",
            "void* p = malloc(4);",
            "std::thread t;",
            "system(\"rm -rf /\");",
            "FILE* f = fopen(\"x\", \"r\");",
            "int s = socket(0, 0, 0);",
            "asm(\"nop\");",
            "pthread_create(0, 0, 0, 0);",
            "execve(\"/bin/sh\", 0, 0);",
            "extern int x;",
            "__builtin_trap();",
        ] {
            assert!(!check_logic(bad).is_empty(), "should refuse: {bad}");
        }
        // comments do not count, and partial words do not match
        assert!(check_logic("// system() is forbidden\nint newest = 1; int readers = 2;").is_empty());
    }

    const GOOD_HEAD: &str = r##""title": "T", "palette": { "bg": "#000000", "panel": "#000000", "accent": "#ffffff", "text": "#ffffff" }, "knobStyle": "round""##;

    fn ui_reasons(text: &str) -> Vec<String> {
        check_ui_json(text).into_iter().map(|v| v.to_string()).collect()
    }

    fn assert_refused(text: &str, wanted: &[&str]) {
        let reasons = ui_reasons(text);
        assert!(!reasons.is_empty(), "should refuse: {text}");
        for w in wanted {
            assert!(reasons.iter().any(|r| r.contains(w)), "no reason containing {w:?} in {reasons:?}");
        }
    }

    fn with_layout(layout: &str) -> String {
        format!("{{ {GOOD_HEAD}, \"layout\": {layout} }}")
    }

    #[test]
    fn a_full_interface_passes() {
        let good = r##"{
          "title": "  Good Interface  ",
          "palette": { "bg": "#1F1B2E", "panel": "#29243b", "accent": "#8FBF9A", "text": "#EDEBE3" },
          "knobStyle": "arc", "style": "retro", "futureField": { "ignored": true },
          "layout": { "width": 720.0, "height": 1600, "items": [
            { "key": "Tone/Drive", "x": 0, "y": 0, "size": "s" },
            { "key": "Mix", "x": 720, "y": 1600, "size": "l", "label": "Blend \ud83c\udf9b\ufe0f", "hidden": false },
            { "key": "Échelle", "x": 24, "y": 1.6e2, "size": "m", "label": " Forty characters exactly, no more, okay ", "hidden": true }
          ] }
        }"##;
        assert_eq!(ui_reasons(good), Vec::<String>::new());
        // every field beyond the first three is optional
        assert!(ui_reasons(&format!("{{ {GOOD_HEAD} }}")).is_empty());
        // lengths are code points, as zod 4 counts them: 60 emoji (120 UTF-16 units) is a 60 character title
        let emoji_title = |n: usize| format!("{{ {} }}", GOOD_HEAD.replace("\"T\"", &format!("\"{}\"", "\u{1F39B}".repeat(n))));
        assert!(ui_reasons(&emoji_title(60)).is_empty());
        assert_refused(&emoji_title(61), &["title must be 1 to 60 characters after trimming spaces (it has 61)"]);
        // the shell's own examples pass here too
        for example in ["tape-drift", "poly-saw", "acid-line", "stomp-fuzz", "glass-chorus", "checks/good-ui"] {
            let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../shell/examples").join(example).join("ui.json");
            let text = std::fs::read_to_string(&path).unwrap();
            assert_eq!(ui_reasons(&text), Vec::<String>::new(), "{}", path.display());
        }
    }

    #[test]
    fn a_broken_interface_is_refused_with_a_reason_per_problem() {
        assert_refused("{ \"title\": \"x\", }", &["ui.json: the file is not valid JSON"]);
        assert_refused("[1, 2, 3]", &["the file must hold one JSON object"]);
        assert_refused(
            "{ \"palette\": { \"bg\": \"#000000\" } }",
            &["title is missing", "palette.panel is missing", "palette.text is missing", "knobStyle is missing"],
        );
        assert_refused(
            r##"{ "title": "C", "palette": { "bg": "#EEE5D3", "panel": "#E3D8C2", "accent": "orange", "text": "#2A262" }, "knobStyle": "round" }"##,
            &["palette.accent must be a colour as #rrggbb (got \"orange\")", "palette.text must be a colour as #rrggbb"],
        );
        assert_refused(
            r##"{ "title": "S", "palette": { "bg": "#EEE5D3", "panel": "#E3D8C2", "accent": "#C8843A", "text": "#2A2620" }, "knobStyle": "square", "style": "steampunk" }"##,
            &["knobStyle must be round or arc", "style must be one of vintage, minimal, pedal, playful, retro"],
        );
        assert_refused(&format!("{{ {GOOD_HEAD}, \"style\": null }}"), &["style must be one of"]);
        assert_refused(
            r##"{ "title": "   ", "palette": [], "knobStyle": "round" }"##,
            &["title must be 1 to 60 characters after trimming spaces (it has 0)", "palette must be an object"],
        );
        let long_title = GOOD_HEAD.replace("\"T\"", &format!("\"{}\"", "x".repeat(61)));
        assert_refused(&format!("{{ {long_title} }}"), &["title must be 1 to 60 characters after trimming spaces (it has 61)"]);
        assert_refused(&format!("{{ {} }}", GOOD_HEAD.replace("\"T\"", "42")), &["title must be text (got 42)"]);
    }

    #[test]
    fn a_broken_layout_is_refused() {
        assert_refused(
            &with_layout(r#"{ "width": 800, "height": 2000, "items": [ { "key": "Drive", "x": 3.5, "y": -8, "size": "xl" }, { "key": "Mix", "x": 721, "y": 24, "size": "m" } ] }"#),
            &[
                "layout.width must be 720 (got 800)",
                "layout.height must be a whole number from 120 to 1600 (got 2000)",
                "layout.items[0].x must be a whole number from 0 to 720 (got 3.5)",
                "layout.items[0].y must be a whole number from 0 to 1600 (got -8)",
                "layout.items[0].size must be s, m or l (got \"xl\")",
                "layout.items[1].x must be a whole number from 0 to 720 (got 721)",
            ],
        );
        assert_refused(
            &with_layout(r#"{ "width": 720, "height": 240, "items": [ { "key": "", "x": 24, "y": 24, "size": "m" }, { "key": "Mix", "x": 24, "y": 24, "size": "m", "label": "A label that runs to forty one characters", "hidden": "yes" } ] }"#),
            &[
                "layout.items[0].key must be 1 to 200 characters (it has 0)",
                "layout.items[1].label must be 1 to 40 characters after trimming spaces (it has 41)",
                "layout.items[1].hidden must be true or false",
            ],
        );
        assert_refused(&with_layout(r#"{ "width": 720, "height": 240, "items": { "key": "Mix" } }"#), &["layout.items must be a list"]);
        assert_refused(&with_layout(r#"{ "width": 720, "height": 240.5, "items": [] }"#), &["layout.height must be a whole number"]);
        assert_refused(&with_layout(r#"{ "width": 720, "height": 240, "items": [ 7 ] }"#), &["layout.items[0] must be an object (got 7)"]);
        assert_refused(&with_layout(r#"{ "height": 240, "items": [] }"#), &["layout.width must be 720 (got nothing)"]);
        assert_refused(&with_layout("\"wide\""), &["layout must be an object"]);
        // JSON has no NaN, and a lone surrogate is not text
        assert_refused(&with_layout(r#"{ "width": 720, "height": NaN, "items": [] }"#), &["not valid JSON"]);
        assert_refused(&format!("{{ {GOOD_HEAD}, \"style\": \"\\ud800\" }}"), &["not valid JSON"]);
        let items = |n: usize| (0..n).map(|i| format!(r#"{{ "key": "K{i}", "x": 0, "y": 0, "size": "s" }}"#)).collect::<Vec<_>>().join(", ");
        assert_refused(
            &with_layout(&format!(r#"{{ "width": 720, "height": 1600, "items": [{}] }}"#, items(129))),
            &["layout.items may hold at most 128 items (it has 129)"],
        );
        assert!(ui_reasons(&with_layout(&format!(r#"{{ "width": 720, "height": 1600, "items": [{}] }}"#, items(128)))).is_empty());
    }

    #[test]
    fn an_interface_larger_than_64_kb_is_refused() {
        let big = format!("{{ {GOOD_HEAD}, \"padding\": \"{}\" }}", "x".repeat(UI_MAX_BYTES));
        assert_eq!(ui_reasons(&big), vec!["ui.json: the file is larger than 64 KB".to_string()]);
    }

    #[test]
    fn only_a_real_interface_is_written() {
        assert_eq!(ui_json_text(&Value::Null), None);
        assert_eq!(ui_json_text(&serde_json::json!({})), None);
        let text = ui_json_text(&serde_json::json!({ "title": "T" })).unwrap();
        assert!(text.contains("\"title\": \"T\""));
    }

    #[test]
    fn the_project_directory_check_reads_ui_json() {
        let dir = std::env::temp_dir().join(format!("pangea-safety-ui-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("dsp.dsp"), "import(\"stdfaust.lib\");\nprocess = _, _;\n").unwrap();
        assert!(check_project_dir(&dir).unwrap().is_empty(), "no ui.json is fine");
        std::fs::write(dir.join("ui.json"), format!("{{ {GOOD_HEAD} }}")).unwrap();
        assert!(check_project_dir(&dir).unwrap().is_empty());
        std::fs::write(dir.join("ui.json"), [b'{', 0xe9, b'}']).unwrap();
        assert_eq!(check_project_dir(&dir).unwrap()[0].to_string(), "ui.json: the file is not UTF-8 text");
        std::fs::write(dir.join("ui.json"), "{ \"title\": 1 }").unwrap();
        assert!(check_project_dir(&dir).unwrap().iter().any(|v| v.to_string() == "ui.json: title must be text (got 1)"));
        std::fs::remove_file(dir.join("ui.json")).unwrap();
        std::fs::write(dir.join("elsewhere.json"), format!("{{ {GOOD_HEAD} }}")).unwrap();
        std::os::unix::fs::symlink(dir.join("elsewhere.json"), dir.join("ui.json")).unwrap();
        assert!(check_project_dir(&dir).unwrap()[0].reason.contains("plain file"));
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
