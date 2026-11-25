# Writes what the web panel's footer needs from a project's plugin.json, for build.sh: the owner's
# name and Pangea's address, in plugin.json's own shape ({"owner": {"name": ...}, "site": ...}),
# and nothing else of it. CMake compiles the result into the plugin as data and the editor reads
# it (platform/PangeaWebEditor.h, Credit), so the owner's email address stays out of the plugin.
#
#   write-credit.py <plugin.json> <credit.json>
#
# A name is kept to one line and 60 characters. An address that is not plain http or https is left
# out, and the plugin then links to Pangea's own name. A plugin.json that cannot be read gives an
# empty credit: read-plugin-json.py has already refused it, with the reason.
import json
import re
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as f:
        p = json.load(f)
except Exception:  # noqa: BLE001 - any failure to read means no credit
    p = {}
if not isinstance(p, dict):
    p = {}

owner = p.get("owner")
name = owner.get("name") if isinstance(owner, dict) else None
if isinstance(name, str):
    name = re.sub(r"\s+", " ", re.sub(r"[\x00-\x1f\x7f]", " ", name)).strip()[:60]
else:
    name = ""

# The same form the editor accepts (Credit::isWebAddress): a host, a port and a plain path.
site = p.get("site")
if not (isinstance(site, str) and re.fullmatch(r"https?://[A-Za-z0-9][A-Za-z0-9._~:/-]{0,190}", site)):
    site = None

credit = {"owner": {"name": name}}
if site:
    credit["site"] = site
with open(sys.argv[2], "w", encoding="utf-8") as f:
    json.dump(credit, f, ensure_ascii=False)
    f.write("\n")
