import assert from 'node:assert/strict';
import {captureCapacity,MAX_RUN_BYTES,MAX_SESSION_BYTES,MAX_METADATA_BYTES,MAX_SESSION_FRAMES} from './session-limits.mjs';
const base={frames:117217,recordBytes:1092,nextBytes:1092,stemBytes:67000000};
// The user's notebook hit the former global 128 MB cap; starting a new run
// must work without clearing its original frames or embedded stems.
assert(base.frames*base.recordBytes>128000000);
assert.equal(captureCapacity(base).remaining,Math.floor(MAX_RUN_BYTES/1092));
assert.equal(captureCapacity({...base,runFrames:Math.floor(MAX_RUN_BYTES/1092)}).reason,'run');
assert.equal(captureCapacity(base).reason,null);
assert.equal(captureCapacity({...base,frames:MAX_SESSION_FRAMES}).reason,'session');
const last=captureCapacity({...base,frames:MAX_SESSION_FRAMES-1});assert.equal(last.remaining,1);
assert.equal(captureCapacity({...base,stemBytes:MAX_SESSION_BYTES-MAX_METADATA_BYTES-24-base.frames*1092}).remaining,0);
// Promotion of old feature-only frames to stereo width must be budgeted too.
const promoted=captureCapacity({frames:400000,recordBytes:24,nextBytes:1092,stemBytes:500000000});
assert.equal(promoted.sessionFrames,Math.floor((MAX_SESSION_BYTES-MAX_METADATA_BYTES-24-500000000)/1092)-400000);
// Never append the frame that would put a run over its replay-safe limit.
const runLimit=Math.floor(MAX_RUN_BYTES/1092);
assert.equal(captureCapacity({...base,runFrames:runLimit-1}).remaining,1);
assert.equal(captureCapacity({...base,runFrames:runLimit}).remaining,0);
assert((runLimit+1)*1092>MAX_RUN_BYTES);assert(runLimit*1092<=MAX_RUN_BYTES);
console.log('PASS: append after old global cap, per-run rollover, full notebooks, stem budget and mixed record widths');
