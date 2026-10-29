// Writes tests/scenarios.txt: the settings and clock runs the cross-check compares.
// Deterministic (own seeded generator), so re-running gives the same file.
// usage: node make_scenarios.js > ../scenarios.txt
'use strict';
let s = 0x12345678;
function r() { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }
const ri = (a, b) => a + Math.floor(r() * (b - a + 1));       // integer a..b
const pick = (arr) => arr[Math.floor(r() * arr.length)];
const RATE_TICKS = [960, 480, 240, 160, 120, 80, 60, 40];
const GATES = [0.05, 0.1, 0.25, 0.33, 0.5, 0.75, 1, 1.5, 2, 3];

const lines = [];
lines.push('# Scenario grammar: space-separated tokens, applied in order.');
lines.push('#   mode=midi|both  root scale minor voicing output density steps rate octave gate seed melody model chords  (=value)');
lines.push('#   note=N (a played MIDI note transposes)  tempo=BPM  run=TICKS (clock in 20-tick steps)  jump=T (move the clock)  stop');

// 1. Default settings, 200 seeds, four passes of the pattern.
for (let seed = 0; seed < 200; ++seed) lines.push(`mode=midi seed=${seed} run=7680`);

// 2. Every scale x minor x voicing x root with a few seeds.
for (let scale = 0; scale < 4; ++scale)
  for (let minor = 0; minor < 2; ++minor)
    for (let voicing = 0; voicing < 5; ++voicing)
      for (let root = 0; root < 12; root += 1)
        lines.push(`mode=midi scale=${scale} minor=${minor} voicing=${voicing} root=${root} seed=${ri(0, 99999)} density=${ri(4, 16)} run=3840`);

// 3. Random full-parameter scenarios with changes while running.
for (let i = 0; i < 1400; ++i) {
  const mode = r() < 0.7 ? 'midi' : 'both';
  const t = [`mode=${mode}`];
  t.push(`scale=${ri(0, 3)}`, `minor=${ri(0, 1)}`, `voicing=${ri(0, 4)}`, `root=${ri(0, 11)}`);
  t.push(`seed=${r() < 0.3 ? ri(0, 99) : ri(0, 99999)}`);
  if (r() < 0.45) t.push(`melody=${r() < 0.3 ? ri(1, 99) : ri(1, 99999)}`);
  t.push(`output=${ri(0, 2)}`, `density=${ri(0, 16)}`, `steps=${r() < 0.5 ? 16 : ri(1, 16)}`, `rate=${ri(0, 7)}`, `octave=${ri(1, 5)}`, `gate=${pick(GATES)}`);
  t.push(`chords=${r() < 0.7 ? 1 : 0}`);
  t.push(`model=${r() < 0.5 ? 7 : ri(0, 6)}`);
  if (r() < 0.35) t.push(`note=${r() < 0.2 ? pick([0, 1, 12, 127, 126, 100]) : ri(36, 84)}`);
  if (r() < 0.2) t.push(`tempo=${pick([60, 90, 120, 128, 140, 174, 200])}`);
  const rt = () => RATE_TICKS[Number((t.slice().reverse().find(x => x.startsWith('rate=')) || 'rate=4').slice(5))];
  const len = () => Number((t.slice().reverse().find(x => x.startsWith('steps=')) || 'steps=16').slice(6));
  t.push(`run=${Math.min(30000, rt() * len() * 2)}`);
  // changes while running
  const nChanges = ri(1, 4);
  for (let k = 0; k < nChanges; ++k) {
    const c = ri(0, 14);
    if (c === 0) t.push(`density=${ri(0, 16)}`);
    else if (c === 1) t.push(`chords=${ri(0, 1)}`);
    else if (c === 2) t.push(`output=${ri(0, 2)}`);
    else if (c === 3) t.push(`steps=${ri(1, 16)}`);
    else if (c === 4) t.push(`rate=${ri(0, 7)}`);
    else if (c === 5) t.push(`octave=${ri(1, 5)}`);
    else if (c === 6) t.push(`gate=${pick(GATES)}`);
    else if (c === 7) t.push(`seed=${ri(0, 99999)}`);
    else if (c === 8) t.push(`melody=${ri(0, 99999)}`);
    else if (c === 9) t.push(`scale=${ri(0, 3)}`);
    else if (c === 10) t.push(`minor=${ri(0, 1)}`);
    else if (c === 11) t.push(`voicing=${ri(0, 4)}`);
    else if (c === 12) t.push(`model=${ri(0, 7)}`);
    else if (c === 13) t.push(`note=${ri(24, 96)}`);
    else t.push(`tempo=${pick([70, 100, 133, 150])}`);
    t.push(`run=${ri(2, 24) * 20 * ri(1, 6)}`);
  }
  if (r() < 0.25) t.push('stop', `jump=${ri(0, 20) * 480}`, `run=${ri(8, 40) * 120}`);
  else if (r() < 0.1) t.push(`jump=${ri(0, 40) * 20}`, `run=${ri(8, 40) * 60}`);   // jump without stop: the original leaves notes held until the clock catches up
  lines.push(t.join(' '));
}
process.stdout.write(lines.join('\n') + '\n');
