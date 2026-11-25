# Reads and validates a project's plugin.json for build.sh and check-project.sh.
# Prints shell assignments (NAME=..., SLUG=..., ...) or PLUGIN_ERROR=... with the reasons.
# FX_PLUGIN_CODE and FX_BUNDLE_ID are empty unless the kind is "midi".
import json
import re
import shlex
import sys

try:
    with open(sys.argv[1]) as f:
        p = json.load(f)
except Exception as e:  # noqa: BLE001 - any failure to read is reported the same way
    print("PLUGIN_ERROR=" + shlex.quote("plugin.json is not valid JSON: %s" % e))
    sys.exit(0)

if not isinstance(p, dict):
    print("PLUGIN_ERROR=" + shlex.quote("plugin.json must hold one object"))
    sys.exit(0)

errors = []


def need(key, pattern, what):
    v = str(p.get(key, ""))
    if not re.fullmatch(pattern, v):
        errors.append("%s must be %s (got %r)" % (key, what, v))
    return v


# The web app writes "bundleIdentifier"; the examples and the first draft of the format say "bundleId".
# Either is fine, and com.pangea.<slug> is the default.
if "bundleId" not in p:
    p["bundleId"] = p.get("bundleIdentifier") or ("com.pangea." + str(p.get("slug", "")))
# A version may be the project's version number (1, 2, 3 ...) rather than three numbers.
# It becomes 0.<n>.0, which still grows with every version so Logic refreshes its cache.
if isinstance(p.get("version"), int) or re.fullmatch(r"\d+", str(p.get("version", ""))):
    p["version"] = "0.%d.0" % int(p["version"])

name = need("name", r"[^\x00-\x1f\"\\/]{1,64}", "1 to 64 printable characters without quotes or slashes")
slug = need("slug", r"[a-z0-9][a-z0-9-]{0,63}", "lower case letters, digits and hyphens")
kind = need("kind", r"effect|instrument|midi", "effect, instrument or midi (a MIDI tool)")
bundle = need("bundleId", r"[A-Za-z0-9][A-Za-z0-9.-]{2,127}", "a reverse domain name such as com.pangea.<slug>")
manu = need("manufacturerCode", r"[A-Za-z0-9]{4}", "four characters")
code = need("pluginCode", r"[A-Z][A-Za-z0-9]{3}", "four characters, the first upper case")
version = need("version", r"\d+\.\d+\.\d+", "three numbers such as 0.1.0, or a whole version number")
# The description reaches CMake and the bundle's Info.plist, so only plain printable ASCII
# goes through, with the characters that mean something to a shell, CMake or XML dropped.
description = re.sub(r'[^\x20-\x7e]|[;$<>&"\\`|{}()\[\]]', "", str(p.get("description", "")))[:200]

# A MIDI tool builds as two plugins (docs/midi-tools.md): <name>, an instrument with MIDI out
# (AU aumu and VST3), and <name> FX, an AU MIDI effect (aumi) for Logic. The second needs its
# own code and bundle identifier, so a host never mistakes one for the other.
fx_code = fx_bundle = ""
if kind == "midi":
    if "fxBundleId" not in p:
        p["fxBundleId"] = str(p.get("bundleId", "")) + ".fx"
    fx_code = need("fxPluginCode", r"[A-Z][A-Za-z0-9]{3}", "four characters, the first upper case")
    fx_bundle = need("fxBundleId", r"[A-Za-z0-9][A-Za-z0-9.-]{2,127}", "a reverse domain name such as com.pangea.<slug>.fx")
    if fx_code == code:
        errors.append("fxPluginCode must differ from pluginCode (both are %r)" % code)
    if fx_bundle == bundle:
        errors.append("fxBundleId must differ from bundleId (both are %r)" % bundle)

if errors:
    print("PLUGIN_ERROR=" + shlex.quote("; ".join(errors)))
    sys.exit(0)

for k, v in [("NAME", name), ("SLUG", slug), ("KIND", kind), ("BUNDLE_ID", bundle),
             ("MANUFACTURER_CODE", manu), ("PLUGIN_CODE", code), ("VERSION", version),
             ("DESCRIPTION", description), ("FX_PLUGIN_CODE", fx_code), ("FX_BUNDLE_ID", fx_bundle)]:
    print("%s=%s" % (k, shlex.quote(v)))
