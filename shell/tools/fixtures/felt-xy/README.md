Two Hands in the Felt look with its XY pad marked by hand, for one test held on both sides
(docs/felt-look.md). `marks.json` names the controls marked `[xy:x]` and `[xy:y]` in place
of the pair the panel would choose by name (Tension and Length); `look.json` is the Felt look as
`shell/platform/web/mapping.js` has it (FELT_LOOK), with Two Hands' title.

- `tools/test-host.sh` builds `test-two-hands` from a copy of `examples/two-hands` with these marks
  and this look, and `tools/host-check` reads the pad's pair from the plugin's own web panel.
- `src/lib/ui/felt-xy.test.ts` puts the same marks on the same controls on the site's side, draws
  them through the builder's data, and holds `look.json` to FELT_LOOK.

Both expect Balance across and Amount up.
