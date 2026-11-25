#!/bin/bash
# test-meters.sh: the meters and step lights of a plugin built on its own (decision 91), offline.
#
#   tools/test-meters.sh
#
# Builds the two meter fixtures (tools/fixtures/meters: the Step Drive Filter, made by the
# Pangea team to decision 83's convention, and Meter Keys, an instrument with a meter in its voice and one in its effect
# section) as a member's plugin is built for a project's own install, with build.sh --verify, into a
# scratch build folder, and runs pangea-render meter-check on each: what the build taps, each block
# counted once, every value a number, nothing tapped in an instrument's voice. Then it finds the
# meters and the step display in the tree each build hands its page, with the page's own code
# (faustMeters in platform/web/mapping.js), and holds them to expected.json, what the site finds in
# the same code (src/lib/faust/plugin-meters.test.ts holds that file to the site's rules).
#
# The web panel itself, its meters drawn and moving, is checked in the Pangea plugin
# (tools/test-host.sh, in the GUI session): a plugin built on its own has the same editor and the
# same page. Installs nothing. Prints one line a check and exits 1 when any fails.
set -euo pipefail
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/pangea-meters-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
FAILED=0
ok() { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; FAILED=1; }
command -v node >/dev/null || { echo "node not found"; exit 1; }
cp "$SHELL_DIR/platform/web/mapping.js" "$OUT/mapping.mjs"

for fixture in "step-drive 6" "meter-keys 1"; do
    name="${fixture% *}"
    taps="${fixture#* }"
    echo "== $name"
    line="$(PANGEA_BUILD_DIR="$OUT/build" bash "$SHELL_DIR/build.sh" "$SHELL_DIR/tools/fixtures/meters/$name" --verify \
            2> "$OUT/$name.err" | tee "$OUT/$name.log" | tail -1)" || true
    if ! python3 -I -c 'import json,sys; d=json.loads(sys.argv[1]); sys.exit(0 if d.get("ok") and d.get("verify") == "passed" else 1)' "$line" 2>/dev/null; then
        bad "$name did not build and pass build.sh --verify: $line"
        tail -5 "$OUT/$name.err" | sed 's/^/      /'
        continue
    fi
    ok "$name builds and passes build.sh --verify, with the library's panel"
    RENDER="$OUT/build/pangea-render_artefacts/Release/pangea-render"
    [ -x "$RENDER" ] || RENDER="$OUT/build/pangea-render_artefacts/pangea-render"
    if (cd "$OUT" && "$RENDER" meter-check --expect "$taps" --tree "$name.tree.json") > "$OUT/$name.meter.log" 2>&1; then
        ok "$(head -1 "$OUT/$name.meter.log" | sed 's/^meter-check: //'), each block counted, every value a number"
    else
        bad "meter-check on $name"
        sed 's/^/      /' "$OUT/$name.meter.log"
        continue
    fi
    if found="$(node --input-type=module -e '
        import { readFileSync } from "node:fs";
        import { pathToFileURL } from "node:url";
        const [mapping, treeFile, expectedFile, name] = process.argv.slice(1);
        const { faustMeters } = await import(pathToFileURL(mapping).href);
        const page = JSON.parse(readFileSync(treeFile, "utf8"));
        const want = JSON.parse(readFileSync(expectedFile, "utf8"))[name];
        const found = faustMeters(page.tree, page.kind);
        const meters = found.meters.map((m) => ({ key: m.key, label: m.label, groups: m.groups.slice(1),
          min: m.min, max: m.max, unit: m.unit, kind: m.kind }));
        const playheads = found.playheads.map((p) => p.slot);
        console.log(meters.map((m) => m.key + " " + m.kind).join(", ") + (playheads.length ? "; step display on slot " + playheads.join() : ""));
        const same = page.kind === want.kind && JSON.stringify(meters) === JSON.stringify(want.meters)
          && JSON.stringify(playheads) === JSON.stringify(want.playheads);
        process.exit(same ? 0 : 1);
    ' "$OUT/mapping.mjs" "$OUT/$name.tree.json" "$SHELL_DIR/tools/fixtures/meters/expected.json" "$name" 2>&1)"; then
        ok "its page finds what the site finds in the same code: $found"
    else
        bad "its page finds something else than the site: $found"
    fi
done

[ "$FAILED" = "0" ] && echo "all meter checks passed" || echo "meter checks failed"
exit "$FAILED"
