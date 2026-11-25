//! The pinned tools, compiled in from `desktop/tools.json`: the Faust and Ninja the app carries
//! (built by `desktop/scripts/build-faust.sh`) and the CMake and JUCE it fetches on the member's
//! Mac (`tools.rs`). Each is one release at its official address with its SHA-256.

use std::sync::OnceLock;

use serde::{Deserialize, Serialize};

const TOOLS_JSON: &str = include_str!("../../tools.json");

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Pin {
    pub version: String,
    pub url: String,
    pub sha256: String,
    pub bytes: u64,
    pub licence: String,
    /// CMake only: where its `cmake` is inside the unpacked release.
    #[serde(default)]
    pub bin_dir: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Pins {
    pub faust: Pin,
    pub ninja: Pin,
    pub cmake: Pin,
    pub juce: Pin,
}

pub fn pins() -> &'static Pins {
    static PINS: OnceLock<Pins> = OnceLock::new();
    PINS.get_or_init(|| serde_json::from_str(TOOLS_JSON).expect("desktop/tools.json is valid"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn faust_is_the_version_the_native_build_is_pinned_to() {
        assert_eq!(Some(pins().faust.version.clone()), crate::prereqs::pinned_faust_version());
    }

    /// library/faust-versions.json says where the native Faust comes from now: the app carries it.
    #[test]
    fn the_faust_pin_names_where_the_apps_faust_comes_from() {
        let v: serde_json::Value = serde_json::from_str(include_str!("../../../library/faust-versions.json")).unwrap();
        let source = v["native"]["source"].as_str().unwrap();
        assert!(source.contains("desktop/scripts/build-faust.sh") && source.contains("without LLVM"), "{source}");
    }

    #[test]
    fn juce_is_the_version_the_shell_builds_with() {
        let build_sh = include_str!("../../../shell/build.sh");
        assert!(build_sh.contains(&format!("JUCE_VERSION=\"{}\"", pins().juce.version)), "shell/build.sh pins another JUCE");
        let build_host = include_str!("../../../shell/build-host.sh");
        assert!(build_host.contains(&format!("--branch {}", pins().juce.version)), "shell/build-host.sh names another JUCE");
    }

    /// Under `set -o pipefail`, `codesign -dv ... | grep -q ...` fails whenever grep has found its
    /// line and stopped reading before codesign finished writing: packaging failed at random. The
    /// scripts save the output first and search it with a here-string.
    #[test]
    fn the_packaging_scripts_never_pipe_into_grep_q() {
        for (name, text) in [
            ("package-mac.sh", include_str!("../../scripts/package-mac.sh")),
            ("notarise-mac.sh", include_str!("../../scripts/notarise-mac.sh")),
            ("build-faust.sh", include_str!("../../scripts/build-faust.sh")),
            ("stage-payload.sh", include_str!("../../scripts/stage-payload.sh")),
        ] {
            assert!(text.contains("set -euo pipefail"), "{name} no longer sets pipefail");
            for (n, line) in text.lines().enumerate() {
                let code = line.split(" #").next().unwrap_or(line);
                let piped_grep_q = code.split('|').skip(1).any(|part| {
                    let words: Vec<&str> = part.split_whitespace().collect();
                    words.first() == Some(&"grep") && words.iter().any(|w| w.starts_with('-') && !w.starts_with("--") && w.contains('q'))
                });
                assert!(!piped_grep_q, "{name}:{}: a pipe into grep -q under pipefail: {line}", n + 1);
            }
        }
    }

    #[test]
    fn every_pin_is_an_official_https_release_with_a_sha256() {
        let p = pins();
        for (name, pin, host) in [
            ("faust", &p.faust, "https://github.com/grame-cncm/faust/releases/download/"),
            ("ninja", &p.ninja, "https://github.com/ninja-build/ninja/releases/download/"),
            ("cmake", &p.cmake, "https://github.com/Kitware/CMake/releases/download/"),
            ("juce", &p.juce, "https://github.com/juce-framework/JUCE/releases/download/"),
        ] {
            assert!(pin.url.starts_with(host), "{name}: {}", pin.url);
            assert!(pin.url.contains(&pin.version), "{name}: the address names another version");
            assert_eq!(pin.sha256.len(), 64, "{name}");
            assert!(pin.sha256.chars().all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase()), "{name}");
            assert!(pin.bytes > 0, "{name}");
        }
        assert_eq!(p.cmake.bin_dir.as_deref(), Some("CMake.app/Contents/bin"));
    }
}
