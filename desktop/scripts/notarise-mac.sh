#!/bin/bash
# notarise-mac.sh: signs Pangea.app with a Developer ID, has Apple notarise it, staples the ticket
# and checks the result, then zips it again for the site. For the owner who holds the Apple
# developer account; nobody else can run it, and it refuses to start without his details.
#
#   PANGEA_SIGNING_IDENTITY="Developer ID Application: <Name> (<TEAMID>)" \
#   PANGEA_NOTARY_PROFILE="<keychain profile>" \
#   desktop/scripts/notarise-mac.sh [path to Pangea.app]
#
# Run desktop/scripts/package-mac.sh first: it builds the app (ad-hoc signed, with the hardened
# runtime and the tools it carries), checks it and writes downloads/. This script takes that app
# (by default desktop/src-tauri/target/release/bundle/macos/Pangea.app) and:
#
#   1. checks the identity is in the keychain and the notary profile answers;
#   2. signs inside out, each with the Developer ID, the hardened runtime and a secure timestamp:
#      every Mach-O file in Contents/Resources and Contents/Frameworks (none today), then the tools
#      in Contents/MacOS (faust, ninja), then the app itself with src-tauri/Entitlements.plist;
#   3. checks the signatures (codesign --verify --deep --strict) and that none is ad hoc;
#   4. submits a zip to Apple's notary service and waits (usually a few minutes), printing Apple's
#      log when it is not accepted;
#   5. staples the ticket to the app and checks it: stapler validate, and Gatekeeper's own
#      assessment (spctl), which must say "Notarized Developer ID";
#   6. zips the stapled app into downloads/ in place of the ad-hoc one and writes
#      downloads/desktop.json again with its size, SHA-256 and "notarised": true.
#
# The profile is made once with Apple's notarytool, which keeps the credentials in the login
# keychain (an app-specific password or an App Store Connect API key, never in this repository):
#
#   xcrun notarytool store-credentials "<keychain profile>" --apple-id <id> --team-id <TEAMID>
#
# docs/setup-a-mac.md, "Notarising the app", has the steps. Plugins are never signed here: Pangea
# builds them on each member's Mac (decision 2).
set -euo pipefail

DESKTOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$DESKTOP_DIR/.." && pwd)"
OUT_DIR="$REPO_DIR/downloads"
ENTITLEMENTS="$DESKTOP_DIR/src-tauri/Entitlements.plist"
APP="${1:-$DESKTOP_DIR/src-tauri/target/release/bundle/macos/Pangea.app}"

IDENTITY="${PANGEA_SIGNING_IDENTITY:-}"
PROFILE="${PANGEA_NOTARY_PROFILE:-}"
if [ -z "$IDENTITY" ] || [ -z "$PROFILE" ]; then
  cat >&2 <<'TEXT'
notarise-mac.sh needs two things from the environment, and will not run without them:
  PANGEA_SIGNING_IDENTITY  the Developer ID Application identity in your keychain, as
                           `security find-identity -v -p codesigning` prints it
  PANGEA_NOTARY_PROFILE    the keychain profile made with `xcrun notarytool store-credentials`
TEXT
  exit 2
fi
case "$IDENTITY" in
  "Developer ID Application:"*) ;;
  *) echo "PANGEA_SIGNING_IDENTITY must be a Developer ID Application identity, not: $IDENTITY" >&2; exit 2 ;;
esac
[ -d "$APP" ] || { echo "no app at $APP; run desktop/scripts/package-mac.sh first" >&2; exit 2; }
[ -f "$ENTITLEMENTS" ] || { echo "no $ENTITLEMENTS" >&2; exit 2; }
[ -f "$OUT_DIR/desktop.json" ] || { echo "no $OUT_DIR/desktop.json; run desktop/scripts/package-mac.sh first" >&2; exit 2; }

echo "== 1. the identity and the notary profile"
security find-identity -v -p codesigning | grep -F "\"$IDENTITY\"" >/dev/null \
  || { echo "the keychain has no valid identity \"$IDENTITY\"" >&2; exit 1; }
xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null \
  || { echo "notarytool could not use the keychain profile \"$PROFILE\"" >&2; exit 1; }

sign() {   # sign <path> [extra codesign arguments]
  local path="$1"; shift
  codesign --force --sign "$IDENTITY" --options runtime --timestamp "$@" "$path"
  echo "signed $(basename "$path")"
}

# Saved first, then searched: a pipe into grep -q under pipefail fails when grep stops reading early.
is_macho() { local kind; kind="$(file -b "$1")"; [[ "$kind" == *Mach-O* ]]; }

echo "== 2. signing inside out"
# Mach-O files that are not the app's own executables: none today, but a library or tool added to
# the resources later must be signed before the app is, or notarisation refuses it.
while IFS= read -r -d '' f; do
  if is_macho "$f"; then sign "$f"; fi
done < <(find "$APP/Contents/Resources" "$APP/Contents/Frameworks" -type f -print0 2>/dev/null)
MAIN="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Contents/Info.plist")"
while IFS= read -r -d '' f; do
  [ "$(basename "$f")" = "$MAIN" ] && continue
  if is_macho "$f"; then sign "$f"; fi
done < <(find "$APP/Contents/MacOS" -type f -print0)
sign "$APP" --entitlements "$ENTITLEMENTS"

echo "== 3. checking the signatures"
codesign --verify --deep --strict --verbose=2 "$APP"
while IFS= read -r -d '' f; do
  if is_macho "$f"; then
    details="$(codesign -dv --verbose=4 "$f" 2>&1)"
    grep -q "Authority=Developer ID Application" <<< "$details" || { echo "$f is not signed with the Developer ID" >&2; exit 1; }
    grep -q 'flags=.*runtime' <<< "$details" || { echo "$f is not signed with the hardened runtime" >&2; exit 1; }
    grep -q '^Timestamp=' <<< "$details" || { echo "$f has no secure timestamp" >&2; exit 1; }
  fi
done < <(find "$APP/Contents" -type f -print0)

echo "== 4. notarising (Apple usually answers within a few minutes)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/pangea-notarise.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
ditto -c -k --sequesterRsrc --keepParent "$APP" "$WORK/Pangea.zip"
xcrun notarytool submit "$WORK/Pangea.zip" --keychain-profile "$PROFILE" --wait --output-format json > "$WORK/submit.json" \
  || true
cat "$WORK/submit.json"
STATUS="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("status", ""))' "$WORK/submit.json" 2>/dev/null || true)"
ID="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("id", ""))' "$WORK/submit.json" 2>/dev/null || true)"
if [ "$STATUS" != "Accepted" ]; then
  echo "Apple did not accept the app (status: ${STATUS:-none}). Apple's log:" >&2
  [ -n "$ID" ] && xcrun notarytool log "$ID" --keychain-profile "$PROFILE" >&2 || true
  exit 1
fi

echo "== 5. stapling and checking"
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"
ASSESS="$(spctl --assess --type execute --verbose=4 "$APP" 2>&1 || true)"
echo "$ASSESS"
grep -q "source=Notarized Developer ID" <<< "$ASSESS" || { echo "Gatekeeper does not accept the app as notarised" >&2; exit 1; }

echo "== 6. the download"
FILE="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["file"])' "$OUT_DIR/desktop.json")"
rm -f "$OUT_DIR/$FILE"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$OUT_DIR/$FILE"
INFO="$OUT_DIR/desktop.json" ZIP="$OUT_DIR/$FILE" python3 -I - <<'PY'
import hashlib, json, os
e = os.environ
info = json.load(open(e["INFO"]))
data = open(e["ZIP"], "rb").read()
info["bytes"] = len(data)
info["sha256"] = hashlib.sha256(data).hexdigest()
info["notarised"] = True
with open(e["INFO"], "w") as f:
    json.dump(info, f, indent=2)
    f.write("\n")
print(json.dumps(info))
PY
echo "== notarised, stapled and zipped into $OUT_DIR; deploy with infra/azure/03-deploy-app.sh to offer it"
