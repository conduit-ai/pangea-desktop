// The search of the list (decision 85). This is the desktop app's copy of the rule the
// site's library page uses (src/lib/catalogue/search.ts in the web app) and the Pangea
// plugin uses (shell/platform/PangeaHost.h, "The list's search"); docs/pangea-plugin.md
// states it once. A change here is a change in three places.
//
// The rule. A plugin's text is in three parts, the most telling first:
//   1. its category: the name, the category's other names, and those of its tags that name
//      a kind of the category (a compressor, filed under Dynamics): `categoryName` and `keywords`
//   2. its own name, its tags and its kind
//   3. its summary, its description and its owner's name
// Every word of what the person typed must start a word of that text. Each word counts as
// the number of the first part it is found in, and the plugin's rank is the sum: the lower,
// the better. So "delay" lists the delays first, then what is named or tagged delay, then
// what only mentions one.
//
// Words: lower case; "iz" reads as "is" on both sides (equalizer, harmonizer); anything
// that is not a letter, a digit or a hyphen parts two words. A hyphenated word in the text
// counts as each of its parts and as the parts joined (lo-fi: lo, fi, lofi); typed, it is
// only the parts joined. A typed word of more than three letters loses a last "s" (delays).

import type { Project } from "./types";

/** One plugin's text, in the three parts of the rule. */
export type SearchParts = readonly [string, string, string];

const SEPARATOR = /[^\p{L}\p{N}-]+/u;

function chunks(text: string): string[][] {
  return text
    .toLowerCase()
    .replaceAll("iz", "is")
    .split(SEPARATOR)
    .map((chunk) => chunk.split("-").filter((part) => part !== ""))
    .filter((parts) => parts.length > 0);
}

/** The words of a plugin's text. */
export function textWords(text: string): string[] {
  const words: string[] = [];
  for (const parts of chunks(text)) {
    words.push(...parts);
    if (parts.length > 1) words.push(parts.join(""));
  }
  return words;
}

/** The words of what a person typed. */
export function searchWords(query: string): string[] {
  return chunks(query).map((parts) => {
    const word = parts.join("");
    return word.length > 3 && word.endsWith("s") ? word.slice(0, -1) : word;
  });
}

/** The rank of a plugin for the typed words, the lower the better; null when a word is nowhere. */
export function matchRank(words: readonly string[], parts: SearchParts): number | null {
  if (words.length === 0) return 0;
  const text = parts.map(textWords);
  let rank = 0;
  for (const word of words) {
    const found = text.findIndex((part) => part.some((has) => has.startsWith(word)));
    if (found < 0) return null;
    rank += found + 1;
  }
  return rank;
}

/** What the list calls a kind: a MIDI tool (docs/midi-tools.md) is "MIDI tool", the others as they are. */
export function kindLabel(kind: string): string {
  return kind === "midi" ? "MIDI tool" : kind;
}

/** A project's text in the three parts of the rule. */
export function searchPartsOf(p: Project): SearchParts {
  return [
    [p.categoryName ?? "", ...p.keywords].join(" "),
    [p.name, ...p.tags, kindLabel(p.kind)].join(" "),
    [p.summary ?? "", p.description ?? "", p.ownerDetails.name ?? p.ownerDetails.email ?? p.owner].join(" "),
  ];
}

/**
 * The projects that match the typed words and, when one is chosen, are filed under that
 * category: the best first, and projects of one rank in the order they came in.
 */
export function findProjects(projects: readonly Project[], query: string, category: string | null): Project[] {
  const words = searchWords(query);
  const ranked: Array<{ project: Project; rank: number }> = [];
  for (const project of projects) {
    if (category !== null && project.category !== category) continue;
    const rank = matchRank(words, searchPartsOf(project));
    if (rank !== null) ranked.push({ project, rank });
  }
  // Array.prototype.sort is stable, so the list's own order holds within a rank.
  return ranked.sort((a, b) => a.rank - b.rank).map((entry) => entry.project);
}

/** The categories the projects are filed under, by name, with how many each holds. */
export function categoriesIn(projects: readonly Project[]): Array<{ id: string; name: string; count: number }> {
  const counts = new Map<string, { id: string; name: string; count: number }>();
  for (const p of projects) {
    if (!p.category) continue;
    const held = counts.get(p.category);
    if (held) held.count += 1;
    else counts.set(p.category, { id: p.category, name: p.categoryName ?? p.category, count: 1 });
  }
  return [...counts.values()].sort((a, b) => a.name.localeCompare(b.name, "en"));
}
