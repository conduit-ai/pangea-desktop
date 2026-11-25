import type { ConfirmRequest } from "../lib/types";

type Props = {
  request: ConfirmRequest;
  onAnswer: (ok: boolean) => void;
};

/** Shown before someone else's code is compiled, and before anything a link started. */
export function ConfirmInstall({ request: r, onAnswer }: Props) {
  return (
    <div className="modal-backdrop" role="dialog" aria-modal="true" aria-labelledby="confirm-title">
      <div className="modal">
        <h2 id="confirm-title">
          Install {r.projectName} v{r.version}?
        </h2>
        <p>
          <b>{r.projectName}</b>, version {r.version}, by <b className="selectable">{r.owner}</b>.
        </p>
        {r.fromLink && <p className="muted">A link asked Pangea to install this.</p>}
        <p>
          Pangea will compile and install code from {r.ownedByMe ? "your account" : "this person"} as a plugin on this Mac. Only
          continue if you trust this project.
        </p>
        {r.kind === "midi" && !r.ownedByMe ? (
          <p className="muted">This is a MIDI tool, which is a logic module. MIDI tools from other members are not built yet, so this install will be refused after the check.</p>
        ) : (
          r.hasLogic &&
          !r.ownedByMe && (
            <p className="muted">This project has a logic module. Logic modules from other members are not built yet, so this install will be refused after the check.</p>
          )
        )}
        <div className="row" style={{ justifyContent: "flex-end", marginTop: 14 }}>
          <button onClick={() => onAnswer(false)}>Cancel</button>
          <button className="primary" onClick={() => onAnswer(true)} autoFocus>
            Install
          </button>
        </div>
      </div>
    </div>
  );
}
