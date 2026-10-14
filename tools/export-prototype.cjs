// Exports the prototype's factory presets and a step-by-step reference run of each one.
// The C++ tests replay the same presets and must match the prototype step for step.
//
// Usage (needs Playwright with Chromium):  node tools/export-prototype.cjs
const { chromium } = require('playwright');
const { readFileSync, writeFileSync, mkdirSync } = require('node:fs');
const { join } = require('node:path');
const { tmpdir } = require('node:os');
const { pathToFileURL } = require('node:url');

const root = join(__dirname, '..');
const STEPS = 128;
const PROB_SEED = 1234;

// Expose the prototype's internals to the page once it has set itself up.
const html = readFileSync(join(root, 'docs/prototype/patchbay.html'), 'utf8').replace(
  '\nrenderLib();',
  '\nwindow.__ps={FACTORY,FSUBS,clone,normPages,normMidi,computeRoutes,evalGraph,setStep:v=>{gstep=v;},stop:()=>setPlay(false)};\nrenderLib();');

(async () => {
const browser = await chromium.launch();
const page = await browser.newPage();
// Probability modules call Math.random; replace it with the same mulberry32 stream the C++ engine uses.
await page.addInitScript(() => {
  const mk = s => () => { s |= 0; s = s + 0x6D2B79F5 | 0; let t = Math.imul(s ^ s >>> 15, 1 | s); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
  let r = mk(1);
  Math.random = () => r();
  window.__seedRandom = s => { r = mk(s); };
});
const tmp = join(tmpdir(), 'patternspace-prototype-export.html');
writeFileSync(tmp, html);
await page.goto(pathToFileURL(tmp).href, { waitUntil: 'load' });

const out = await page.evaluate(({ STEPS, PROB_SEED }) => {
  const P = window.__ps;
  P.stop();
  const prep = g => { P.normPages(g); P.normMidi(g); P.computeRoutes(g); return g; };
  const presets = P.FACTORY.map(f => ({ name: f.name, patch: P.clone(prep(f.make())) }));
  const subsystems = P.FSUBS.map(s => P.clone(s));
  const reference = {};
  for (const f of P.FACTORY) {
    const G = prep(f.make());
    const rec = {};
    const record = (g, path) => {
      for (const n of g.nodes) {
        const key = path + n.id;
        let v;
        if (n.k === 'midi') v = (n.rt && n.rt.v || []).join(',');
        else if (n.k === 'sample') v = String(n.rt && n.rt.trig || 0);
        else if (n.k === 'audio') continue;
        else v = (g._o[n.id] || []).join(',');
        (rec[key] || (rec[key] = [])).push(v);
        if (n.k === 'sub') record(n.g, key + '/');
      }
    };
    window.__seedRandom(PROB_SEED);
    for (let s = 0; s < STEPS; s++) { P.setStep(s); P.evalGraph(G); record(G, ''); }
    reference[f.name] = rec;
  }
  return { presets, subsystems, reference };
}, { STEPS, PROB_SEED });
await browser.close();

const slug = s => s.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
mkdirSync(join(root, 'presets/factory'), { recursive: true });
out.presets.forEach((p, i) =>
  writeFileSync(join(root, 'presets/factory', `${String(i + 1).padStart(2, '0')}-${slug(p.name)}.json`),
    JSON.stringify({ name: p.name, patch: p.patch }, null, 2) + '\n'));
mkdirSync(join(root, 'presets/subsystems'), { recursive: true });
out.subsystems.forEach((s, i) =>
  writeFileSync(join(root, 'presets/subsystems', `${String(i + 1).padStart(2, '0')}-${slug(s.name)}.json`), JSON.stringify(s, null, 2) + '\n'));
writeFileSync(join(root, 'tests/data/reference-steps.json'),
  JSON.stringify({ steps: STEPS, probabilitySeed: PROB_SEED, presets: out.reference }) + '\n');
console.log(`Wrote ${out.presets.length} presets, ${out.subsystems.length} subsystems and the reference run.`);
})();
