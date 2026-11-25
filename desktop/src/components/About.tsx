import { useEffect, useState } from "react";
import { backend, errorMessage } from "../lib/backend";
import type { About as AboutInfo } from "../lib/types";

/**
 * Where Conduit AI publishes the Pangea shell and this app as source (decision 127). The one place
 * the address is written: the owners may choose another name for the repository.
 */
export const SHELL_SOURCE_URL = "https://github.com/conduit-ai/pangea-desktop";

/**
 * About Pangea: the app's version, the shell's licence, and each tool it carries or fetches with
 * its licence. The Faust compiler the app carries is under the GPL, so the app carries its complete
 * source too (docs/desktop-setup.md, "Licences"), and this says where it is. The shell is under the
 * GNU AGPL 3.0 or later, carried as source and published at SHELL_SOURCE_URL.
 */
export function AboutPanel() {
  const [about, setAbout] = useState<AboutInfo | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    backend
      .about()
      .then(setAbout)
      .catch((e) => setError(errorMessage(e)));
  }, []);

  if (error) return <p className="error selectable">{error}</p>;
  if (!about) return null;

  return (
    <div className="about selectable">
      <p>
        Pangea {about.appVersion}
        {about.commit ? <span className="muted"> ({about.commit})</span> : <span className="muted"> (a development build)</span>}
      </p>
      <h3>Pangea's shell</h3>
      <p>
        The shell builds your plugins and the Pangea plugin on this Mac. It is Conduit AI's, built on JUCE, and free software under the
        GNU Affero General Public License, version 3 or later. Pangea carries it as source, and Conduit AI publishes it with this app at{" "}
        <a
          href={SHELL_SOURCE_URL}
          onClick={(e) => {
            e.preventDefault();
            backend.openUrl(SHELL_SOURCE_URL).catch((err) => setError(errorMessage(err)));
          }}
        >
          {SHELL_SOURCE_URL.replace(/^https:\/\//, "")}
        </a>
        .
      </p>
      <p>
        Its Felt look and six of its examples are by another author, who also offers them under the same licence, at your choice
        between that and his own.
      </p>
      <h3>The tools Pangea uses</h3>
      <ul className="licences">
        <li>
          <b>Faust {about.faust}</b>, by GRAME. {about.carriesTools ? "Pangea carries it" : "A packaged Pangea carries it"} and uses it to
          turn your plugin's Faust code into C++. The compiler is free software under the GNU General Public License, version 2 or later. Its
          headers and libraries are under the GNU Lesser General Public License, version 2.1 or later, with the Faust exception. Its
          complete source and the script that built it are inside Pangea, with the licence texts.
        </li>
        <li>
          <b>Ninja {about.ninja}</b>. {about.carriesTools ? "Pangea carries it" : "A packaged Pangea carries it"} to run builds. Apache
          License 2.0.
        </li>
        <li>
          <b>CMake {about.cmake}</b>, by Kitware. Pangea downloads it from Kitware's release the first time it is needed. BSD 3-Clause
          licence.
        </li>
        <li>
          <b>JUCE {about.juce}</b>. Pangea downloads it from JUCE's release the first time it is needed, and your plugins are built with it
          on this Mac. GNU Affero General Public License 3, or JUCE's own licence.
        </li>
        <li>
          <b>Apple's build tools</b> come from Apple's own installer, under Apple's licence, which you accept when you install them.
        </li>
      </ul>
      {about.licencesDir && (
        <button onClick={() => backend.openLicences().catch((e) => setError(errorMessage(e)))}>Show the licences and Faust's source</button>
      )}
    </div>
  );
}
