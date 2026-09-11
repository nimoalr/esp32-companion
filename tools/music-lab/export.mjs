// Local, dependency-free notebook export for C replay and optional ML tools.
import fs from 'node:fs';
import path from 'node:path';
import {unpack} from './trace.mjs';
import {timeline,wav,pcmStats,evaluate} from './replay.mjs';
if(process.argv.length!==4){console.error('usage: node tools/music-lab/export.mjs session.mcal output-directory');process.exit(1);}
const input=fs.readFileSync(process.argv[2]),{meta,records}=unpack(input.buffer.slice(input.byteOffset,input.byteOffset+input.byteLength));
fs.mkdirSync(process.argv[3],{recursive:true});
for(const [i,t] of meta.tracks.entries()){
 if(!Number.isInteger(t.startFrame)||!Number.isInteger(t.endFrame)||t.startFrame<0||t.endFrame>records.length||t.endFrame<t.startFrame)throw new Error('Invalid track boundaries');
 const p=records.slice(t.startFrame,t.endFrame),map=timeline(p),stats=pcmStats(p),stem=path.join(process.argv[3],String(i+1).padStart(3,'0'));
 if(stats.samples)fs.writeFileSync(stem+'.wav',wav(p,{map}),{flag:'wx'});
 const {stemAudio,...metadata}=t;
 for(const [name,data] of Object.entries(stemAudio?.stems||{}))fs.writeFileSync(stem+'-'+name.toLowerCase()+'.flac',data,{flag:'wx'});
 fs.writeFileSync(stem+'.json',JSON.stringify({...metadata,timebase:'seconds from first captured frame; sequence gaps retained as silence',sampleRate:16000,channels:2,pcmStats:stats,gaps:map.gaps,evaluation:evaluate(map,t.annotations||[])},null,2),{flag:'wx'});
 console.log(`${stem}: ${map.duration.toFixed(3)}s, ${stats.samples?'stereo WAV':'features only'}, ${map.gaps.length} gap/boundary markers`);
}
