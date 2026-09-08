// Compare a new C analyser replay with the original device decisions on human labels.
import fs from 'node:fs';
import {unpack} from './trace.mjs';
import {timeline,evaluate} from './replay.mjs';
if(process.argv.length!==5){console.error('usage: node tools/music-lab/compare-replay.mjs session.mcal track-number reanalysis.csv');process.exit(1);}
const b=fs.readFileSync(process.argv[2]),{meta,records}=unpack(b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength));
const index=Number(process.argv[3])-1,t=meta.tracks[index];if(!Number.isInteger(index)||!t)throw new Error('Invalid track number (starts at 1)');
const map=timeline(records.slice(t.startFrame,t.endFrame)),original=evaluate(map,t.annotations||[]);
const lines=fs.readFileSync(process.argv[4],'utf8').trim().split(/\r?\n/),header=lines.shift().split(','),time=header.indexOf('time_ms'),dance=header.indexOf('would_dance');
if(time<0||dance<0)throw new Error('Expected audio_replay CSV with time_ms and would_dance');
const flags=new Map();for(const line of lines){const c=line.split(','),ms=Number(c[time]),d=Number(c[dance]);if(!Number.isFinite(ms)||![0,1].includes(d)||ms%16)throw new Error('Invalid replay row');flags.set(ms/16,d?64:0);}
for(const e of map.entries){if(!flags.has(e.frame))throw new Error('Replay does not cover the entire exported track');e.a={...e.a,flags:flags.get(e.frame)};}
console.log(JSON.stringify({track:t.track,timebase:'exported microphone sample clock',labelledOriginal:original,labelledReanalysis:evaluate(map,t.annotations||[]),note:'Reanalysis starts with fresh detector state. Unlabelled ranges, missing audio and conflicting human labels are excluded.'},null,2));
