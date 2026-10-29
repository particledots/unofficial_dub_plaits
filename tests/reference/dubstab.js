// DUB STAB — generative, controlled dub techno stabs. Max for Live script.
// Sequencer and harmony from the DUB STAB PLAITS firmware (dub.cc), ported to
// Max js, with a root-note menu, four minor scales and a minor-chords-only switch.
// Copyright 2026 Narti Live. MIT license.
//
// One script, three devices (the first argument):
//   midi   Dub Stab MIDI   — the sequencer sends chords / ACID line / roots as MIDI
//   synth  Dub Stab Synth  — MIDI in plays the gen~ synth (chords or ACID)
//   both   Dub Stab Plaits — the sequencer plays the gen~ synth
//
// Inlet messages: ticks <f>, tempo <f>, res <f>, state <i>, midi <i>,
//   root, scale, minor, voicing, output, density, steps, rate, octave, gate,
//   seed, newpattern, model, chords.
// Outlets: 0 MIDI bytes, 1 gen~ messages, 2 chord text, 3 seed (after NEW),
//   4 pattern text, 5 random sound (harmonics/timbre/morph), 6 melody seed.

autowatch = 0;
inlets = 1;
outlets = 7;

var MODE = (jsarguments.length > 1) ? String(jsarguments[1]) : "midi";

// ------------------------------------------------------------------ settings

var SCALES = [
  [0, 2, 3, 5, 7, 8, 10],   // Natural minor (Aeolian)
  [0, 2, 3, 5, 7, 9, 10],   // Dorian
  [0, 1, 3, 5, 7, 8, 10],   // Phrygian
  [0, 2, 3, 5, 7, 8, 11]    // Harmonic minor
];
var NOTE_NAMES = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
// Root menu C..B -> semitones from C, kept within -5..+6 so the register stays
// where the module has it (G = -5, as in the G minor firmwares).
var ROOT_OFFSET = [0, 1, 2, 3, 4, 5, 6, -5, -4, -3, -2, -1];

// Ticks (480 per quarter) per step:  1/2 1/4 1/8 1/8T 1/16 1/16T 1/32 1/32T
var RATE_TICKS = [960, 480, 240, 160, 120, 80, 60, 40];
var GRID = 20;                       // the clock metro runs every 20 ticks

var S = {
  root: 0, scale: 0, minor: 0, voicing: 0, output: 0,
  density: 5, length: 16, rate: 4, octave: 3, gate: 0.5,
  seed: 1, melody: 0, model: 0, transpose: 0,
  chords: 1         // CHORDS switch: 0 = the sequencer plays mono notes instead of chords
};

var NUM_STEPS = 16;
var RANK_BIAS = [0.5, 0.0, 1.3, 0.35, 0.15, 0.0, 1.2, 0.25,
                 0.4, 0.0, 1.25, 0.35, 0.1, 0.0, 1.1, 0.45];
var BASE_WEIGHTS = [4.0, 0.7, 1.5, 2.5, 2.0, 2.0, 1.0];
// DEEP (as the G minor firmware v1.1): open dub techno shapes in root position.
var DEEP_STEPS = [
  [0, 4, 6, 9, 11],   // DUB m7      1 5 b7 b3' 5'
  [0, 4, 6, 8, 9],    // m9 open     1 5 b7 9 b3'
  [0, 3, 6, 9],       // m11 (4ths)  1 4 b7 b3'
  [0, 4, 6, 10],      // 7sus4       1 5 b7 4'
  [0, 4, 8],          // sus2        1 5 9
  [0, 4, 7],          // fifths      1 5 8
  [0, 4, 9],          // open minor  1 5 b3'
  [0, 2, 4, 8]        // m(add9)     1 b3 5 9
];
var DEEP_WEIGHT = [2.0, 2.0, 2.2, 1.2, 1.0, 1.0, 0.7, 0.5];
var DEEP_NO_THIRD = [3, 3, 3, 3, 4, 5, 4, 4];   // on degrees with a major 3rd
var DEEP_DEGREE_WEIGHTS = [5.0, 0.6, 0.6, 2.5, 2.0, 1.2, 1.5];
var DEEP = 4;                                   // Voicing menu index
var DUB_STEPS = [0, 4, 6, 9, 11];            // root 5th 7th 3rd' 5th'
var TRIAD_STEPS = [0, 2, 4];
var SEVENTH_STEPS = [0, 2, 4, 6];
var ACID_OFFSETS = [0, 0, 7, 0, 4, 0, 2, 7];

// ------------------------------------------------------------------ random

// Max's js engine predates Math.imul.
function imul(a, b) {
  var ah = (a >>> 16) & 0xffff, al = a & 0xffff, bh = (b >>> 16) & 0xffff, bl = b & 0xffff;
  return ((al * bl) + (((ah * bl + al * bh) << 16) >>> 0)) | 0;
}
var rngState = 1;
function seedRandom(s) { rngState = (s * 2654435761 + 0x9e3779b9) >>> 0; if (!rngState) rngState = 1; }
function rand() {                              // mulberry32, 0..1
  rngState = (rngState + 0x6D2B79F5) >>> 0;
  var t = rngState;
  t = imul(t ^ (t >>> 15), t | 1);
  t ^= t + imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}

// ------------------------------------------------------------------ harmony

function floorDiv7(d) { return Math.floor(d / 7); }
function mod7(d) { return d - 7 * floorDiv7(d); }
function scaleNote(d) { return SCALES[S.scale][mod7(d)] + 12 * floorDiv7(d); }

// True when the triad on this degree has a minor third (minor or diminished).
function isMinorDegree(d) {
  var third = scaleNote(d + 2) - scaleNote(d);
  return third === 3;
}

// Minor-only: a degree with a major chord is replaced by the nearest minor one,
// trying the relative minor a third below first (it shares most notes).
function allowedDegree(d) {
  if (!S.minor || isMinorDegree(d)) return d;
  var tries = [-2, 2, -1, 1, -3, 3];
  for (var i = 0; i < tries.length; ++i) if (isMinorDegree(d + tries[i])) return d + tries[i];
  return d;
}

function keyOffset() { return ROOT_OFFSET[S.root]; }

// voicing: 0 = DUB, 1-3 = triad (inversion 0-2), 4-7 = seventh (inversion 0-3)
function buildChord(degree, voicing) {
  degree = allowedDegree(degree);
  var steps = DUB_STEPS, inversion = 0;
  if (voicing >= 10) {                          // DEEP shape 0..7
    var sh = voicing - 10;
    if (scaleNote(degree + 2) - scaleNote(degree) === 4) sh = DEEP_NO_THIRD[sh];
    steps = DEEP_STEPS[sh];
  } else if (voicing >= 1 && voicing <= 3) { steps = TRIAD_STEPS; inversion = voicing - 1; }
  else if (voicing >= 4 && voicing <= 7) { steps = SEVENTH_STEPS; inversion = voicing - 4; }
  var n = steps.length;
  var base = 12 * (S.octave + 1) + keyOffset();
  var raw = [], i;
  for (i = 0; i < n; ++i) raw.push(base + scaleNote(degree + steps[i]));
  var root = raw[0];
  for (i = 0; i < inversion; ++i) raw[i] += 12;
  raw.sort(function (a, b) { return a - b; });
  var centreSum = (base + 7 + 12 * floorDiv7(degree)) * n;
  var sum = 0;
  for (i = 0; i < n; ++i) sum += raw[i];
  var bestShift = 0, best = 1e9;
  for (var shift = -12; shift <= 12; shift += 12) {
    var dd = Math.abs(sum + shift * n - centreSum);
    if (dd < best) { best = dd; bestShift = shift; }
  }
  for (i = 0; i < n; ++i) raw[i] += bestShift;
  return { notes: raw, root: root, degree: degree, voicing: voicing };
}

function semitonesToDegrees(st) {
  st = Math.round(st);
  var o = Math.floor(st / 12), pc = st - 12 * o;
  var sc = SCALES[S.scale], best = 0, bestD = 99;
  for (var i = 0; i < 7; ++i) {
    var d = Math.abs(pc - sc[i]);
    if (d < bestD) { bestD = d; best = i; }
  }
  return best + 7 * o;
}

function acidNote(degree, stepIndex, rank) {
  degree = allowedDegree(degree);
  var h = (rank * 5 + stepIndex * 3 + P.acidShift[stepIndex]) & 7;
  var d = degree + ACID_OFFSETS[h];
  var base = 12 * (S.octave + 2) + keyOffset();     // one octave above the chords
  return { note: base + scaleNote(d), slide: (h === 3 || h === 6) };
}

// CHORDS off: one note, from the chord root up to two octaves, in the chords register.
function monoNote(degree, st) {
  degree = allowedDegree(degree);
  return 12 * (S.octave + 1) + keyOffset() + scaleNote(degree + P.mono[st]);
}
function noteName(n) { return NOTE_NAMES[((n % 12) + 12) % 12] + Math.floor(n / 12 - 1); }

function chordName(c) {
  if (c.voicing >= 10) return deepName(c);
  var d = c.degree;
  var r = scaleNote(d), t = scaleNote(d + 2) - r, f = scaleNote(d + 4) - r, s = scaleNote(d + 6) - r;
  var q;
  if (t === 3 && f === 7) q = "m"; else if (t === 4 && f === 7) q = "";
  else if (t === 3 && f === 6) q = "dim"; else if (t === 4 && f === 8) q = "aug"; else q = "?";
  var seventh = c.voicing === 0 || c.voicing >= 4;
  if (seventh) {
    if (q === "m") q = s === 10 ? "m7" : "mMaj7";
    else if (q === "") q = s === 11 ? "maj7" : "7";
    else if (q === "dim") q = s === 10 ? "m7b5" : "dim7";
    else if (q === "aug") q = s === 11 ? "maj7#5" : "7#5";
  }
  var pc = ((c.root % 12) + 12) % 12;
  var name = NOTE_NAMES[pc] + q;
  var bass = ((c.notes[0] % 12) + 12) % 12;
  if (c.voicing !== 0 && bass !== pc) name += "/" + NOTE_NAMES[bass];
  return name;
}

// Names a chord from its notes (DEEP shapes: sus, 5, m9, m11, add9).
function deepName(c) {
  var pc = ((c.root % 12) + 12) % 12, has = [], i;
  for (i = 0; i < 12; ++i) has[i] = false;
  for (i = 0; i < c.notes.length; ++i) has[(((c.notes[i] - c.root) % 12) + 12) % 12] = true;
  var q;
  if (!has[3] && !has[4]) q = has[5] ? (has[10] ? "7sus4" : (has[11] ? "maj7sus4" : "sus4"))
      : (has[2] ? (has[10] ? "9sus" : "sus2")
      : (has[6] && has[7] ? (has[11] ? "maj7#11" : "(#11)") : (has[6] ? "(b5)" : "5")));
  else if (has[4]) q = has[11] ? "maj7" : (has[10] ? "7" : "");
  else if (has[6] && !has[7]) q = has[10] ? "m7b5" : "dim";
  else if (has[10]) q = has[2] ? "m9" : (has[5] ? "m11" : "m7");
  else q = has[2] ? "m(add9)" : "m";
  return NOTE_NAMES[pc] + q;
}

// ------------------------------------------------------------------ pattern

var P = { degree: [], voicing: [], rank: [], accent: [], mono: [], acidShift: [] };

function pickDegree(avoid) {
  var w = [], total = 0, i;
  for (i = 0; i < 7; ++i) {
    w[i] = (S.minor && !isMinorDegree(i)) ? 0 : (S.voicing === DEEP ? DEEP_DEGREE_WEIGHTS[i] : BASE_WEIGHTS[i]);
    if (i !== avoid) total += w[i];
  }
  var r = rand() * total;
  for (i = 0; i < 7; ++i) {
    if (i === avoid || w[i] === 0) continue;
    r -= w[i];
    if (r <= 0) return i;
  }
  return 0;
}

// Same recipe as the firmware: 1, 2 or 4 chords per pattern, mostly held.
function generate() {
  seedRandom(S.seed);
  var r = rand(), i, k;
  var segments = r < 0.35 ? 1 : (r < 0.75 ? 2 : 4);
  var chord = [];
  chord[0] = rand() < 0.7 ? 0 : pickDegree(-1);
  for (k = 1; k < segments; ++k) chord[k] = pickDegree(chord[k - 1]);
  for (i = 0; i < NUM_STEPS; ++i) {
    var d = chord[Math.floor(i * segments / NUM_STEPS)];
    if (rand() < 0.06) d = pickDegree(d);
    P.degree[i] = d;
  }
  var voicing = [];
  for (k = 0; k < segments; ++k) {
    var q = rand(), a = rand();
    if (S.voicing === 1) voicing[k] = 1 + Math.floor(a * 2.999);
    else if (S.voicing === 2) voicing[k] = 4 + Math.floor(a * 3.999);
    else if (S.voicing === 3) voicing[k] = q < 0.4 ? 1 + Math.floor(a * 2.999) : (q < 0.8 ? 4 + Math.floor(a * 3.999) : 0);
    else voicing[k] = 0;
  }
  if (S.voicing === DEEP) {
    // One shape for the whole pattern 65 % of the time, else one per section.
    var total = 0, oneShape = rand() < 0.65;
    for (i = 0; i < 8; ++i) total += DEEP_WEIGHT[i];
    for (k = 0; k < segments; ++k) {
      if (k > 0 && oneShape) { voicing[k] = voicing[0]; continue; }
      var rr = rand() * total, v = 0;
      while (v < 7 && (rr -= DEEP_WEIGHT[v]) > 0) ++v;
      voicing[k] = 10 + v;
    }
  }
  for (i = 0; i < NUM_STEPS; ++i) P.voicing[i] = voicing[Math.floor(i * segments / NUM_STEPS)];
  var score = [], index = [];
  for (i = 0; i < NUM_STEPS; ++i) { score[i] = RANK_BIAS[i] + rand() * 0.9; index[i] = i; }
  index.sort(function (a, b) { return score[b] - score[a] || a - b; });
  for (i = 0; i < NUM_STEPS; ++i) P.rank[index[i]] = i;
  for (i = 0; i < NUM_STEPS; ++i) P.accent[i] = rand() < (RANK_BIAS[i] > 1.0 ? 0.35 : 0.15) ? 1 : 0;
  // Mono line (CHORDS off): the firmware's random walk over two octaves of the
  // scale, starting from each step's chord root. Melody 0 = the line that comes
  // with the pattern; other melody seeds give new lines (and new ACID lines)
  // over the same chords and rhythm.
  if (S.melody) seedRandom(S.seed * 7919 + S.melody * 104729 + 17);
  for (i = 0; i < NUM_STEPS; ++i) P.acidShift[i] = S.melody ? Math.floor(rand() * 8) : 0;
  var n = rand() < 0.5 ? 0 : 7;
  for (i = 0; i < NUM_STEPS; ++i) {
    var w = rand();
    if (w < 0.25) {
    } else if (w < 0.7) {
      n += rand() < 0.5 ? -1 : 1;
    } else if (w < 0.9) {
      n += rand() < 0.5 ? -2 : 2;
    } else {
      n = (rand() < 0.5 ? 0 : 7) + (rand() < 0.3 ? 4 : 0);
    }
    if (n < 0) n = 1;
    if (n > 13) n = 12;
    P.mono[i] = n;
  }
  showPattern(-1);
}

// ------------------------------------------------------------------ output

var lastStep = -1;
function showPattern(current) {
  var s = "";
  var len = Math.max(1, Math.min(16, S.length));
  for (var i = 0; i < 16; ++i) {
    var on = P.rank[i] < S.density;
    var ch = i >= len ? " " : (on ? (P.accent[i] ? "X" : "x") : ".");
    if (i === current) ch = on ? "#" : "_";
    s += ch;
    if (i === 3 || i === 7 || i === 11) s += " ";
  }
  outlet(4, "set", s);
}

// MIDI out (mode midi): notes held until an off tick.
var pendingOffs = [];   // {tick, note}
function midiNoteOn(note, vel) {
  if (note < 0 || note > 127) return;
  for (var i = pendingOffs.length - 1; i >= 0; --i) {
    if (pendingOffs[i].note === note) { outlet(0, 128); outlet(0, note); outlet(0, 0); pendingOffs.splice(i, 1); }
  }
  outlet(0, 144); outlet(0, note); outlet(0, vel);
}
function scheduleOff(note, tick) { pendingOffs.push({ tick: tick, note: note }); }
function processOffs(tick) {
  for (var i = pendingOffs.length - 1; i >= 0; --i) {
    if (pendingOffs[i].tick <= tick) {
      outlet(0, 128); outlet(0, pendingOffs[i].note); outlet(0, 0);
      pendingOffs.splice(i, 1);
    }
  }
}
function allOff() {
  for (var i = 0; i < pendingOffs.length; ++i) { outlet(0, 128); outlet(0, pendingOffs[i].note); outlet(0, 0); }
  pendingOffs = [];
}

// gen~ events: eight queue slots, so a whole chord fits in one audio vector.
// slot value = (toggle * 4 + type) * 16384 + note * 128 + velocity
//   type 0 chord note, 1 acid note, 2 acid note with slide, 3 acid gate off
var slotToggle = [0, 0, 0, 0, 0, 0, 0, 0];
var slotNext = 0, lastEventTime = 0;
function genEvent(type, note, vel) {
  var now = Date.now();
  if (now - lastEventTime > 20) slotNext = 0;   // a new burst starts at slot 0 (keeps the order)
  lastEventTime = now;
  var k = slotNext;
  slotNext = (slotNext + 1) & 7;
  slotToggle[k] ^= 1;
  note = Math.max(0, Math.min(127, Math.round(note)));
  vel = Math.max(1, Math.min(127, Math.round(vel)));
  outlet(1, "q" + k, (slotToggle[k] * 4 + type) * 16384 + note * 128 + vel);
}

// ------------------------------------------------------------------ clock

var tempoBpm = 120, ticksPerBeat = 480, lastGrid = -1, lastTickFired = false;

function ticks(t) {
  var g = Math.round(t * (480 / ticksPerBeat) / GRID) * GRID;
  if (g === lastGrid) return;
  lastGrid = g;
  processOffs(g);
  var rt = RATE_TICKS[S.rate];
  if (g % rt !== 0) return;
  var n = Math.floor(g / rt);
  var len = Math.max(1, Math.min(16, S.length));
  var st = n % len;
  fireStep(st, g, rt);
}
ticks.immediate = 1;

function stepMs(rt) { return rt / 480 * 60000 / tempoBpm; }

function fireStep(st, g, rt) {
  var plays = P.rank[st] < S.density;
  showPattern(st);
  if (!plays) { lastTickFired = false; return; }
  var accent = P.accent[st];
  var degree = P.degree[st] + S.transpose;
  var c = buildChord(degree, P.voicing[st]);
  var vel = accent ? 127 : 90;
  var useAcid = (MODE === "both") ? (S.model === 7) : (MODE === "midi" && S.output === 1);
  if (useAcid) {
    var a = acidNote(degree, st, P.rank[st]);
    var slide = a.slide && lastTickFired;
    var gateTicks = rt * (slide ? 1.05 : 0.55);
    if (MODE === "midi") {
      midiNoteOn(a.note, vel);
      scheduleOff(a.note, g + Math.max(GRID, Math.round(gateTicks / GRID) * GRID));
    } else {
      outlet(1, "alen", stepMs(rt) * (slide ? 1.05 : 0.55));
      genEvent(slide ? 2 : 1, a.note, vel);
    }
    outlet(2, "set", noteName(a.note) + (slide ? " slide" : "") + "  (" + chordName(c) + ")");
  } else if (!S.chords && !(MODE === "midi" && S.output === 2)) {
    var mn = monoNote(degree, st);
    if (MODE === "midi") {
      midiNoteOn(mn, vel);
      scheduleOff(mn, g + Math.max(GRID, Math.round(rt * S.gate / GRID) * GRID));
    } else {
      genEvent(0, mn, vel);
    }
    outlet(2, "set", noteName(mn) + "  (" + chordName(c) + ")");
  } else if (MODE === "midi") {
    var gt = g + Math.max(GRID, Math.round(rt * S.gate / GRID) * GRID);
    if (S.output === 2) {           // roots: one note, one octave below the chord
      midiNoteOn(c.root - 12, vel);
      scheduleOff(c.root - 12, gt);
    } else {
      for (var i = 0; i < c.notes.length; ++i) { midiNoteOn(c.notes[i], vel); scheduleOff(c.notes[i], gt); }
    }
    outlet(2, "set", chordName(c));
  } else {
    for (var j = 0; j < c.notes.length; ++j) genEvent(0, c.notes[j], vel);
    outlet(2, "set", chordName(c));
  }
  lastTickFired = true;
}

function tempo(t) { if (t > 0) tempoBpm = t; }
tempo.immediate = 1;
function res(r) { if (r > 0) ticksPerBeat = r; }
res.immediate = 1;
function state(s) {
  if (!s) {
    allOff();
    if (MODE === "both") genEvent(3, 0, 1);
    lastGrid = -1;
    lastTickFired = false;
    showPattern(-1);
  }
}
state.immediate = 1;

// ------------------------------------------------------------------ MIDI in

var midiStatus = 0, midiData = [], held = [];
function playsSynth() { return MODE === "synth"; }
function midi(b) {
  b = b & 255;
  if (b >= 0xF8) return;                 // realtime
  if (b & 0x80) { midiStatus = b; midiData = []; return; }
  midiData.push(b);
  var type = midiStatus & 0xF0;
  if ((type === 0x90 || type === 0x80) && midiData.length === 2) {
    var note = midiData[0], vel = midiData[1];
    midiData = [];
    if (type === 0x90 && vel > 0) noteIn(note, vel); else noteOffIn(note);
  } else if (midiData.length >= 2) {
    midiData = [];
  }
}
midi.immediate = 1;

function noteIn(note, vel) {
  if (playsSynth()) {
    if (S.model === 7) {
      var legato = held.length > 0;
      held.push(note);
      outlet(1, "alen", 0);
      genEvent(legato ? 2 : 1, note, vel);
    } else {
      genEvent(0, note, vel);
    }
    return;
  }
  // midi / both: a played note transposes the pattern (C3 = no change), like V/OCT.
  S.transpose = semitonesToDegrees(note - 60);
}

function noteOffIn(note) {
  if (!playsSynth() || S.model !== 7) return;
  var i = held.lastIndexOf(note);
  if (i >= 0) held.splice(i, 1);
  if (held.length === 0) genEvent(3, 0, 1);
  else if (i === held.length) genEvent(2, held[held.length - 1], 100);   // back to the held note
}

// ------------------------------------------------------------------ parameters

function setInt(name, v, lo, hi, regen) {
  v = Math.round(v);
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  if (S[name] === v) return;
  S[name] = v;
  if (regen) generate(); else showPattern(-1);
}

function root(v) { setInt("root", v, 0, 11, false); }
function scale(v) { setInt("scale", v, 0, 3, true); }
function minor(v) { setInt("minor", v, 0, 1, true); }
function voicing(v) { setInt("voicing", v, 0, 4, true); }
function output(v) { setInt("output", v, 0, 2, false); allOff(); }
function density(v) { setInt("density", v, 0, 16, false); }
function steps(v) { setInt("length", v, 1, 16, false); }
function rate(v) { setInt("rate", v, 0, 7, false); }
function octave(v) { setInt("octave", v, 1, 5, false); }
function gate(v) { S.gate = Math.max(0.05, Math.min(2, v)); }
function seed(v) { setInt("seed", v, 0, 99999, true); }
function model(v) {
  v = Math.round(v);
  if (v === S.model) return;
  if (S.model === 7) genEvent(3, 0, 1);
  S.model = v;
  held = [];
}
function chords(v) {
  v = v ? 1 : 0;
  if (v === S.chords) return;
  S.chords = v;
  allOff();
}
function bang() { newPattern(); }
function melody(v) { setInt("melody", v, 0, 99999, true); }
function newmelody(v) {
  if (arguments.length > 0 && Number(v) === 0) return;
  if (Date.now() - loadTime < 1500) return;
  var m = Math.floor(Math.random() * 99999) + 1;
  if (m === S.melody) m = (m % 99999) + 1;
  outlet(6, m);        // to the hidden Melody number box, which sends it back as "melody"
}
// RANDOM: new HARMONICS / TIMBRE / MORPH (like holding RIGHT on the module).
function randomsound(v) {
  if (arguments.length > 0 && Number(v) === 0) return;
  if (Date.now() - loadTime < 1500) return;
  outlet(5, "harmonics", Math.round(5 + Math.random() * 90));
  outlet(5, "timbre", Math.round(5 + Math.random() * 90));
  outlet(5, "morph", Math.round(5 + Math.random() * 90));
}
function newPattern() {
  var s = Math.floor(Math.random() * 99999) + 1;
  if (s === S.seed) s = (s % 99999) + 1;
  outlet(6, 0);        // the pattern's own melody
  outlet(3, s);        // goes to the Seed number box, which sends it back as "seed"
}
// "new" is a reserved word in js, so the NEW button sends "newpattern".
// Ignored for a moment after loading (Live restores the button's value), and
// on the button's release (0).
var loadTime = Date.now();
function newpattern(v) {
  if (arguments.length > 0 && Number(v) === 0) return;
  if (Date.now() - loadTime < 1500) return;
  newPattern();
}

function loadbang() { loadTime = Date.now(); generate(); }
generate();

// node test hook
if (typeof module !== "undefined") module.exports = {
  S: S, P: P, generate: generate, buildChord: buildChord, acidNote: acidNote,
  allowedDegree: allowedDegree, isMinorDegree: isMinorDegree, chordName: chordName,
  semitonesToDegrees: semitonesToDegrees, monoNote: monoNote, ticks: ticks, midi: midi, state: state,
  setMode: function (m) { MODE = m; }
};
