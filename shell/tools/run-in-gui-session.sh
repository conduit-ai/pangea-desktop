#!/bin/bash
# Runs a command inside the user's GUI launchd session (the one Logic and the desktop app live in)
# and prints its output. Needed when the caller is a background session (an SSH login, an agent,
# a cron job): there, Audio Unit lookups reach the system registrar, which knows only Apple's own
# components, so auval cannot see anything in ~/Library/Audio/Plug-Ins/Components.
#
#   tools/run-in-gui-session.sh <command> [args...]
#
# Exit code is the command's own. `launchctl managername` says "Aqua" when a session already is
# the GUI session; then the command is simply run directly.
set -u
[ $# -gt 0 ] || { echo "usage: run-in-gui-session.sh <command> [args...]" >&2; exit 2; }

if [ "$(launchctl managername 2>/dev/null)" = "Aqua" ]; then
    exec "$@"
fi

UID_NUM="$(id -u)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/pangea-gui-run.XXXXXX")"
LABEL="com.pangea.gui-run.$$.$RANDOM"
RUNNER="$WORK/run.sh"
{
    echo '#!/bin/bash'
    printf 'cd %q\n' "$PWD"
    printf '%q ' "$@"
    printf ' > %q 2>&1\n' "$WORK/output.log"
    printf 'echo $? > %q\n' "$WORK/exit"
} > "$RUNNER"
chmod +x "$RUNNER"

cat > "$WORK/job.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>Label</key><string>$LABEL</string>
    <key>ProgramArguments</key><array><string>/bin/bash</string><string>$RUNNER</string></array>
    <key>RunAtLoad</key><true/>
</dict></plist>
EOF

if ! launchctl bootstrap "gui/$UID_NUM" "$WORK/job.plist" 2> "$WORK/bootstrap.err"; then
    echo "run-in-gui-session: could not start the job in gui/$UID_NUM: $(cat "$WORK/bootstrap.err")" >&2
    rm -rf "$WORK"
    exit 2
fi

waited=0
while [ ! -f "$WORK/exit" ] && [ "$waited" -lt 600 ]; do sleep 1; waited=$((waited + 1)); done
launchctl bootout "gui/$UID_NUM/$LABEL" 2>/dev/null

if [ ! -f "$WORK/exit" ]; then
    echo "run-in-gui-session: the command did not finish within 10 minutes" >&2
    cat "$WORK/output.log" 2>/dev/null
    rm -rf "$WORK"
    exit 2
fi
cat "$WORK/output.log"
code="$(cat "$WORK/exit")"
rm -rf "$WORK"
exit "${code:-1}"
