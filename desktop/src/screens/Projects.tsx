import { useEffect, useMemo, useState } from "react";
import { PrereqBanner } from "../components/Prereqs";
import { backend, errorMessage } from "../lib/backend";
import { categoriesIn, findProjects, kindLabel } from "../lib/search";
import type { Check, Project, QueueState } from "../lib/types";

type Props = {
  checks: Check[];
  queue: QueueState;
  canBuild: boolean;
  onInstall: (p: Project) => void;
  onOpenSettings: () => void;
  onSessionExpired: () => void;
};

export function Projects({ checks, queue, canBuild, onInstall, onOpenSettings, onSessionExpired }: Props) {
  const [projects, setProjects] = useState<Project[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [query, setQuery] = useState("");
  // The category chosen in the list's menu, by id; null for every category.
  const [category, setCategory] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  async function load() {
    setLoading(true);
    setError(null);
    try {
      const list = await backend.listProjects();
      list.sort((a, b) => (b.updatedAt ?? "").localeCompare(a.updatedAt ?? "") || a.name.localeCompare(b.name));
      setProjects(list);
    } catch (e) {
      const msg = errorMessage(e);
      if (/not signed in|expired/i.test(msg)) onSessionExpired();
      setError(msg);
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // The categories are counted over what the typed words match, so each number is what
  // choosing that category would show (decision 85; the rule is in ../lib/search.ts).
  const matching = useMemo(() => (projects ? findProjects(projects, query, null) : []), [projects, query]);
  const categories = useMemo(() => {
    const found = categoriesIn(matching);
    if (category !== null && !found.some((c) => c.id === category)) {
      // The category chosen holds nothing now: it stays in the menu, so that it can be left.
      const name = projects?.find((p) => p.category === category)?.categoryName ?? category;
      found.push({ id: category, name, count: 0 });
      found.sort((a, b) => a.name.localeCompare(b.name, "en"));
    }
    return found;
  }, [matching, category, projects]);
  const shown = useMemo(
    () => (category === null ? matching : matching.filter((p) => p.category === category)),
    [matching, category],
  );

  const busy = new Set<string>();
  if (queue.current) busy.add(queue.current.projectId);
  for (const j of queue.queue) busy.add(j.projectId);

  return (
    <div className="projects">
      <PrereqBanner checks={checks} onOpenSettings={onOpenSettings} />
      <div className="toolbar">
        <input
          type="search"
          placeholder="Search by name, category or tag"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          autoFocus
        />
        {categories.length > 0 && (
          <select
            aria-label="Category"
            value={category ?? ""}
            onChange={(e) => setCategory(e.target.value === "" ? null : e.target.value)}
          >
            <option value="">All categories ({matching.length})</option>
            {categories.map((c) => (
              <option key={c.id} value={c.id}>
                {c.name} ({c.count})
              </option>
            ))}
          </select>
        )}
        <button onClick={() => void load()} disabled={loading}>
          {loading ? "Loading…" : "Refresh"}
        </button>
      </div>

      {error && <div className="notice bad selectable">{error}</div>}

      <div className="list">
        {projects === null && !error && <div className="empty">Loading your projects…</div>}
        {projects !== null && shown.length === 0 && (
          <div className="empty">{projects.length === 0 ? "No projects yet. Make one on the web first." : "Nothing matches."}</div>
        )}
        {shown.map((p) => {
          const isBusy = busy.has(p.id);
          return (
            <div className="item project" key={p.id}>
              <div className="grow">
                <div className="row">
                  <span className="name ellipsis">{p.name}</span>
                  {p.categoryName && <span className="badge category">{p.categoryName}</span>}
                  <span className={`badge ${p.kind}`}>{kindLabel(p.kind)}</span>
                  <span className={`badge ${p.visibility}`}>{p.visibility}</span>
                </div>
                <div className="meta">
                  <span>{p.headVersion != null ? `v${p.headVersion}` : "no version yet"}</span>
                  {p.owner && (
                    <span title={p.owner} style={{ whiteSpace: "nowrap" }}>
                      · {p.ownerDetails.name ?? p.ownerDetails.email ?? p.owner}
                    </span>
                  )}
                  {(p.summary || p.description) && <span className="ellipsis">· {p.summary || p.description}</span>}
                </div>
                {p.tags.length > 0 && (
                  <div className="tags">
                    {p.tags.map((tag) => (
                      <span key={tag}>{tag}</span>
                    ))}
                  </div>
                )}
              </div>
              <div className="actions">
                <button
                  className="primary"
                  disabled={!canBuild || isBusy || p.headVersion == null}
                  title={!canBuild ? "Set the shell directory in Settings first" : undefined}
                  onClick={() => onInstall(p)}
                >
                  {isBusy ? "Queued" : "Install in Logic"}
                </button>
                <button onClick={() => void backend.openProjectOnWeb(p.id)}>Open on the web</button>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
