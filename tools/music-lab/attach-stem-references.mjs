// Attach verified level curves to a new notebook; preserve PCM and human labels.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {pack,unpack} from './trace.mjs';
import {timeline,wav} from './replay.mjs';
import {validateReference} from './stem-reference.mjs';
import {AUDIO_STEMS,validateStemAudio} from './stem-audio.mjs';

if(![5,6].includes(process.argv.length)){console.error('usage: node attach-stem-references.mjs original.mcal reference-directory new.mcal [flac-directory]');process.exit(1);}
const input=fs.readFileSync(process.argv[2]);
const {meta,records}=unpack(input.buffer.slice(input.byteOffset,input.byteOffset+input.byteLength));
for(const [i,track] of meta.tracks.entries()){
 if(!Number.isInteger(track.startFrame)||!Number.isInteger(track.endFrame)||track.startFrame<0||track.endFrame>records.length||track.endFrame<=track.startFrame)throw new Error('Invalid track boundaries');
 const p=records.slice(track.startFrame,track.endFrame),map=timeline(p);
 const hash=createHash('sha256').update(wav(p,{map})).digest('hex');
 const reference=JSON.parse(fs.readFileSync(path.join(process.argv[3],`${String(i+1).padStart(3,'0')}-reference.json`),'utf8'));
 track.stemReference=validateReference(reference,map.frames*256,hash);
 if(process.argv[5])track.stemAudio=validateStemAudio({sourceSHA256:hash,stems:Object.fromEntries(AUDIO_STEMS.map(name=>[name,new Uint8Array(fs.readFileSync(path.join(process.argv[5],`${String(i+1).padStart(3,'0')}-${name.toLowerCase()}.flac`)))]))},reference);
}
const output=Buffer.from(await pack(meta,records).arrayBuffer());
fs.writeFileSync(process.argv[4],output,{flag:'wx'});
console.log(`${meta.tracks.length} verified stem references attached; ${output.length-input.length} bytes added. Original file unchanged. ${process.argv[5]?'All stem audio embedded in this notebook.':'Level curves only.'}`);
