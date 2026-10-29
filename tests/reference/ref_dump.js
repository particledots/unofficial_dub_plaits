// Runs the ORIGINAL dubstab.js (unmodified, from the Dub Stab Max for Live devices) on the scenarios in
// tests/scenarios.txt and prints an event log. The C++ port prints the same log (tests/seq_crosscheck.cpp);
// CI fails if the two differ by a single line.
//
// usage: node ref_dump.js <scenarios.txt> > expected.txt
'use strict';
const fs = require('fs'), vm = require('vm'), path = require('path');

const source = fs.readFileSync(path.join(__dirname, 'dubstab.js'), 'utf8');
const scenarios = fs.readFileSync(process.argv[2], 'utf8').split('\n').filter(l => l.trim() && !l.startsWith('#'));

function hex64(x) {
  const dv = new DataView(new ArrayBuffer(8));
  dv.setFloat64(0, x);
  return dv.getBigUint64(0).toString(16).padStart(16, '0');
}

const out = [];

function runScenario(index, line) {
  const tokens = line.trim().split(/\s+/);
  let mode = 'midi';
  for (const tk of tokens) if (tk.startsWith('mode=')) mode = tk.slice(5);

  let label = 0, bytes = [];
  const log = (s) => out.push(label + ' ' + s);
  const outlet = (n, ...a) => {
    if (n === 0) {
      bytes.push(a[0]);
      if (bytes.length === 3) {
        if (bytes[0] === 144) log('ON ' + bytes[1] + ' ' + bytes[2]);
        else if (bytes[0] === 128) log('OFF ' + bytes[1]);
        else log('BADMIDI ' + bytes.join(' '));
        bytes = [];
      }
    } else if (n === 1) {
      if (a[0] === 'alen') log('ALEN ' + hex64(a[1]));
      else {                               // "q<slot>", (toggle*4+type)*16384 + note*128 + vel
        const v = a[1];
        log('GEN ' + (Math.floor(v / 16384) % 4) + ' ' + (Math.floor(v / 128) % 128) + ' ' + (v % 128));
      }
    } else if (n === 2) log('CHORD ' + a[1]);
    else if (n === 4) log('PAT ' + a[1]);
  };

  const ctx = vm.createContext({ jsarguments: ['dubstab.js', mode], outlet });
  const mark = out.length;
  vm.runInContext(source, ctx);            // runs generate() once, like loading the device
  out.length = mark;                       // whatever the load itself printed is not part of the log

  const dumpPattern = () => {
    const P = ctx.P;
    out.push('P d=' + P.degree.join(',') + ' v=' + P.voicing.join(',') + ' r=' + P.rank.join(',') +
             ' a=' + P.accent.join(',') + ' m=' + P.mono.join(',') + ' s=' + P.acidShift.join(','));
  };

  out.push('## ' + index + ' ' + line.trim());
  dumpPattern();
  let t = 0;
  const regen = new Set(['scale', 'minor', 'voicing', 'seed', 'melody']);
  for (const tk of tokens) {
    if (tk === 'stop') { label = t; ctx.state(0); continue; }
    const eq = tk.indexOf('=');
    const key = tk.slice(0, eq), val = parseFloat(tk.slice(eq + 1));
    label = t;
    if (key === 'mode') continue;
    if (key === 'run') { for (let k = 0; k < val; k += 20) { label = t; ctx.ticks(t); t += 20; } continue; }
    if (key === 'jump') { t = val; continue; }
    if (key === 'note') { ctx.midi(0x90); ctx.midi(val); ctx.midi(100); continue; }
    if (key === 'tempo') { ctx.tempo(val); continue; }
    ctx[key](val);                         // root scale minor voicing output density steps rate octave gate seed melody model chords
    if (regen.has(key)) dumpPattern();
  }
  dumpPattern();
}

scenarios.forEach((line, i) => runScenario(i, line));
process.stdout.write(out.join('\n') + '\n');
