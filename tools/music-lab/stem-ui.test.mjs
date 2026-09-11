import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {createStems} from './stem-ui.mjs';
import {silenceFLAC} from './stem-audio.test.mjs';

// Exercise asynchronous job ownership without a browser or an inference model.
const elements=new Map();
function element(id){if(!elements.has(id))elements.set(id,{dataset:{},setAttribute(){}});return elements.get(id);}
const buttons=['original','Drums','Bass','Vocals','Other'].map(name=>({...element(name),dataset:{stem:name}}));
globalThis.document={getElementById:element,querySelectorAll:()=>buttons};
const input=new ArrayBuffer(44),hash=Buffer.from(await webcrypto.subtle.digest('SHA-256',input)).toString('hex');
const reference={format:'music-lab-stems-v1',source:{wavSHA256:hash,sampleRate:16000,channels:2,frames:256},model:'htdemucs.yaml',hopSamples:1600,unit:'0.1 dBFS',levels:Object.fromEntries(['Mixture','Drums','Bass','Vocals','Other'].map(n=>[n,[-500]]))};
const deferred=()=>{let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};};
let pending,posts=0,deletes=0,edits=0,auditions=[],messages=[];
const response=value=>({ok:true,headers:{get:()=> 'application/json'},json:async()=>value});
globalThis.fetch=async(path,options={})=>{
 if(path.endsWith('.flac'))return {ok:true,headers:{get:()=>String(silenceFLAC.length)},arrayBuffer:async()=>silenceFLAC.slice().buffer};
 if(options.method==='POST'){posts++;return response({id:hash,state:'running'});}
 if(options.method==='DELETE'){deletes++;return response({ok:true});}
 if(path==='/api/stems')return response({available:true});
 return response(await pending.promise);
};
const ui=createStems({getWav:()=>input,onEdit:()=>edits++,notice:m=>messages.push(m),onAudio:(...a)=>auditions.push(a),onSeek(){},onSelect(){},redraw(){}});
const first={track:'First',annotations:[{start:0,end:.016,expected:'dance'}]},second={track:'Second'},map={frames:1,duration:.016};
await ui.open(first,map);
ui.setCaptureBusy(true);
await element('stem-analyze').onclick();assert.equal(posts,0,'No analysis during capture');
ui.setCaptureBusy(false);
pending=deferred();const running=element('stem-analyze').onclick();
await new Promise(r=>setImmediate(r));assert.equal(ui.busy,true);
await ui.open(second,map); // Navigate while an earlier track is being processed.
pending.resolve({id:hash,state:'ready',reference});await running;
assert.equal(first.stemReference,reference);assert.equal(second.stemReference,undefined);
assert.deepEqual(first.annotations,[{start:0,end:.016,expected:'dance'}]);assert.equal(edits,1);assert.equal(ui.busy,false);

pending=deferred();const cancelling=element('stem-analyze').onclick();
await new Promise(r=>setImmediate(r));await element('stem-cancel').onclick();
assert.equal(deletes,2);assert.equal(ui.busy,true,'Cancellation waits for the worker to stop');
pending.resolve({id:hash,state:'failed'});await cancelling;
assert.equal(ui.busy,false);assert.equal(second.stemReference,undefined);assert.match(messages.at(-1),/cancelled/);

pending=deferred();pending.resolve({id:hash,state:'ready',reference});
await ui.open(first,map);
element('stem-solo').onclick({target:buttons[1]});assert.match(auditions.at(-1)[0],/^blob:/);
assert.equal(element('stem-lanes').hidden,false);assert.equal(first.stemReference,reference);
assert.deepEqual(first.stemAudio.stems.Drums,silenceFLAC);assert.equal(edits,1);assert.equal(buttons[1].disabled,false);
ui.clear();globalThis.fetch=async()=>{throw new Error('Offline');};await ui.open(first,map);
assert.equal(buttons[1].disabled,false,'Embedded audio works without a server');
console.log('PASS capture guard, job ownership, temporary-file deletion, cancellation and offline embedded audition');
