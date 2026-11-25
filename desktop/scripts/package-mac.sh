#!/bin/bash
# Builds Pangea.app for download and writes it, zipped, to downloads/ at the repository root,
# with downloads/desktop.json describing it. The next infra/azure/03-deploy-app.sh copies
# both into the image, and Settings, The desktop app offers the zip to signed-in members.
#
#   desktop/scripts/package-mac.sh [web address]
#
# The web address is what the app talks to until someone changes it in its Settings. Without
# one, the live site as infra/azure/config.sh names it (its hostname once bound, else the container
# app's own name). A copy without infra/azure, such as the public one, must give the address.
# The app carries Faust and Ninja (stage-payload.sh, then tauri.bundle.conf.json puts them in the
# bundle), under the hardened runtime with Entitlements.plist. It is ad-hoc signed here, not
# notarised (decision 39): macOS asks before the first open. notarise-mac.sh signs it with a
# Developer ID, notarises and staples it, for the owner who holds the Apple account.
set -euo pipefail

DESKTOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$DESKTOP_DIR/.." && pwd)"
OUT_DIR="$REPO_DIR/downloads"
APP="$DESKTOP_DIR/src-tauri/target/release/bundle/macos/Pangea.app"

URL="${1:-}"
if [ -z "$URL" ] && [ ! -f "$REPO_DIR/infra/azure/config.sh" ]; then
  echo "Give the web address the app should talk to, for example: desktop/scripts/package-mac.sh https://pangea.audio" >&2
  exit 2
fi
if [ -z "$URL" ]; then
  # shellcheck source=/dev/null
  source "$REPO_DIR/infra/azure/config.sh" >/dev/null
  FQDN=$(az containerapp show -n "$APP_NAME" -g "$AZ_RG" --query properties.configuration.ingress.fqdn -o tsv 2>/dev/null)
  BOUND=$(az containerapp hostname list -n "$APP_NAME" -g "$AZ_RG" \
    --query "length([?name=='$APP_HOSTNAME' && bindingType=='SniEnabled'])" -o tsv 2>/dev/null || echo 0)
  if [ "$BOUND" = "1" ]; then URL="https://$APP_HOSTNAME"; else URL="https://$FQDN"; fi
fi
case "$URL" in
  https://*|http://localhost:*) ;;
  *) echo "The web address must start with https:// (or be http://localhost:<port>): $URL" >&2; exit 2 ;;
esac
URL="${URL%/}"

if [ -n "$(git -C "$REPO_DIR" status --porcelain -- desktop shell library)" ]; then
  echo "desktop/, shell/ or library/ has changes that are not committed; the download would not match its commit." >&2
  exit 1
fi
COMMIT=$(git -C "$REPO_DIR" rev-parse --short=12 HEAD)

echo "== staging what the app carries"
bash "$DESKTOP_DIR/scripts/stage-payload.sh"
PAYLOAD="$DESKTOP_DIR/src-tauri/target/pangea-payload"

echo "== building Pangea.app for $URL (commit $COMMIT)"
cd "$DESKTOP_DIR"
pnpm install --ignore-workspace --frozen-lockfile
PANGEA_API_BASE_URL="$URL" pnpm tauri build --config src-tauri/tauri.bundle.conf.json

echo "== checking the bundle"
codesign --verify --deep --strict "$APP"
for tool in faust ninja; do
  [ -x "$APP/Contents/MacOS/$tool" ] || { echo "the bundle has no Contents/MacOS/$tool" >&2; exit 1; }
  codesign --verify --strict "$APP/Contents/MacOS/$tool"
  # Saved first, then searched: codesign piped into grep -q under pipefail fails at random when
  # grep stops reading before codesign has finished writing.
  SIGNED="$(codesign -dv "$APP/Contents/MacOS/$tool" 2>&1)"
  grep -q 'flags=.*runtime' <<< "$SIGNED" \
    || { echo "Contents/MacOS/$tool is not signed with the hardened runtime" >&2; exit 1; }
done
SIGNED="$(codesign -dv "$APP" 2>&1)"
grep -q 'flags=.*runtime' <<< "$SIGNED" || { echo "the app is not signed with the hardened runtime" >&2; exit 1; }
cmp -s "$APP/Contents/Resources/pangea/manifest.json" "$PAYLOAD/pangea/manifest.json" \
  || { echo "the bundle's pangea/manifest.json is not the staged one" >&2; exit 1; }
"$APP/Contents/MacOS/faust" --version | head -1
"$APP/Contents/MacOS/ninja" --version
echo "bundle: $(du -sh "$APP" | cut -f1)"
ARCH=$(lipo -archs "$APP/Contents/MacOS/pangea-desktop" 2>/dev/null || lipo -archs "$APP/Contents/MacOS/"*)
case "$ARCH" in
  arm64) ARCH_NAME=arm64 ;;
  x86_64) ARCH_NAME=x86_64 ;;
  *arm64*x86_64*|*x86_64*arm64*) ARCH_NAME=universal ;;
  *) echo "unexpected architecture: $ARCH" >&2; exit 1 ;;
esac
if ! grep -aq "$URL" "$APP/Contents/MacOS/"*; then
  echo "the app does not carry $URL; was PANGEA_API_BASE_URL seen by the build?" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
FILE="Pangea-mac-$ARCH_NAME.zip"
rm -f "$OUT_DIR/$FILE" "$OUT_DIR/desktop.json"
# ditto keeps the bundle exactly as built, signature included; zip would not.
ditto -c -k --sequesterRsrc --keepParent "$APP" "$OUT_DIR/$FILE"

FILE="$FILE" OUT_DIR="$OUT_DIR" URL="$URL" COMMIT="$COMMIT" ARCH_NAME="$ARCH_NAME" \
CONF="$DESKTOP_DIR/src-tauri/tauri.conf.json" MANIFEST="$PAYLOAD/pangea/manifest.json" python3 - <<'PY'
import hashlib, json, os, datetime
e = os.environ
path = os.path.join(e["OUT_DIR"], e["FILE"])
conf = json.load(open(e["CONF"]))
carried = json.load(open(e["MANIFEST"]))
data = open(path, "rb").read()
info = {
    "version": conf["version"],
    "file": e["FILE"],
    "bytes": len(data),
    "sha256": hashlib.sha256(data).hexdigest(),
    "arch": e["ARCH_NAME"],
    "minimumMacOS": conf["bundle"]["macOS"]["minimumSystemVersion"],
    "apiBaseUrl": e["URL"],
    "commit": e["COMMIT"],
    "builtAt": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z"),
    "carries": {"faust": carried["faust"]["version"], "ninja": carried["ninja"]["version"]},
    "notarised": False,
}
with open(os.path.join(e["OUT_DIR"], "desktop.json"), "w") as f:
    json.dump(info, f, indent=2)
    f.write("\n")
print(json.dumps(info))
PY
echo "== written to $OUT_DIR; deploy with infra/azure/03-deploy-app.sh to offer it"
