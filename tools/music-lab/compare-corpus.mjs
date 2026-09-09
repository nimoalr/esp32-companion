// Compare exact C replays with human ranges; keep recorded and fresh-state
// baselines separate. No inference from unlabelled or unsure ranges.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {unpack, summarize, summarizeCorpus} from './trace.mjs';
import {timeline, evaluate} from './replay.mjs';

export function replayRows(text, frames) {
 const lines=text.trim().split(/\r?\n/), header=lines.shift().split(',');
 const ti=header.indexOf('time_ms'), di=header.indexOf('would_dance'), drive=header.indexOf('dance_drive');
 if(ti<0||di<0)throw new Error('Expected audio_replay CSV');
 const rows=lines.map((line,i)=>{
  const c=line.split(',');
  if(c.length!==header.length||c[ti]===''||c[di]==='')throw new Error('Incomplete replay row');
  const time=Number(c[ti]), dance=Number(c[di]);
  if(time!==i*16||![0,1].includes(dance))throw new Error('Replay must contain consecutive 16 ms frames');
  const intensity=drive<0?null:Number(c[drive]);
  if(intensity!==null&&(!Number.isFinite(intensity)||intensity<0||intensity>1))throw new Error('Invalid dance intensity');
  return {dance,drive:intensity};
 });
 if(rows.length!==frames)throw new Error('Replay duration does not match exported microphone timeline');
 return rows;
}

export function compareTrack(records, labels, beforeText, afterText) {
 const map=timeline(records), before=replayRows(beforeText,map.frames), after=replayRows(afterText,map.frames);
 const withRows=rows=>({...map,entries:map.entries.map(e=>({...e,a:{...e.a,flags:rows[e.frame].dance?64:0}}))});
 const beforeMap=withRows(before), afterMap=withRows(after);
 const coverage=rows=>rows.reduce((n,r)=>n+r.dance,0)/rows.length*100;
 const meanDrive=(start,end)=>{
  const selected=map.entries.filter(e=>e.a.hasAudio&&e.start<end&&e.end>start&&after[e.frame].drive!==null);
  let total=0, duration=0;
  for(const e of selected){const dt=Math.min(end,e.end)-Math.max(start,e.start);total+=dt*after[e.frame].drive;duration+=dt;}
  return duration?total/duration:null;
 };
 return {
  duration:map.duration, gaps:map.gaps.length,
  recorded:evaluate(map,labels), before:evaluate(beforeMap,labels), after:evaluate(afterMap,labels),
  wholeTrackDancePercent:{before:coverage(before),after:coverage(after)},
  annotations:labels.map(l=>({...l,recorded:evaluate(map,[l]),before:evaluate(beforeMap,[l]),after:evaluate(afterMap,[l]),meanDrive:meanDrive(l.start,l.end)})),
 };
}

if(process.argv[1]&&fileURLToPath(import.meta.url)===path.resolve(process.argv[1])) {
 if(process.argv.length!==4){console.error('usage: node tools/music-lab/compare-corpus.mjs session.mcal replay-directory\nCSV names: 001-baseline.csv, 001-candidate.csv, etc.');process.exit(1);}
 const b=fs.readFileSync(process.argv[2]);
 const {meta,records}=unpack(b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength));
 const tracks=meta.tracks.map((t,i)=>{
  if(!Number.isInteger(t.startFrame)||!Number.isInteger(t.endFrame)||t.startFrame<0||t.endFrame>records.length||t.endFrame<=t.startFrame)throw new Error('Invalid track boundaries');
  const p=records.slice(t.startFrame,t.endFrame), stem=String(i+1).padStart(3,'0');
  const read=kind=>fs.readFileSync(path.join(process.argv[3],`${stem}-${kind}.csv`),'utf8');
  return {track:t.track,kind:t.kind,style:t.style,summary:summarize(p),...compareTrack(p,t.annotations||[],read('baseline'),read('candidate'))};
 });
 const total={recorded:{},before:{},after:{}};
 for(const t of tracks)for(const variant of Object.keys(total))for(const [key,value] of Object.entries(t[variant]))total[variant][key]=(total[variant][key]||0)+value;
 console.log(JSON.stringify({
  note:'Tuning-corpus results, not held-out accuracy. Whole-track coverage is descriptive only. Human unsure/unlabelled/conflicting ranges and missing PCM are excluded from aggregate label scores. Recorded decisions carry session state; both replays start fresh per track. Drive is a control signal, not a measured dance-intensity ground truth.',
  total, spectra:Object.fromEntries([...new Set(tracks.map(t=>t.kind))].map(kind=>[kind,summarizeCorpus(tracks.filter(t=>t.kind===kind))])),tracks,
 },null,2));
}
