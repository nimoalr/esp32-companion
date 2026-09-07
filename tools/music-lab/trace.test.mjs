import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {decode,parseLine,pack,unpack,summarize} from './trace.mjs';
const [p]=parseLine(readFileSync('tools/host/out/trace-fixture.log','utf8'));
assert.equal(p.length,24);const a=decode(p);assert.equal(a.ms,123456);assert.equal(a.rms,300);assert.equal(a.bpm,150);assert.equal(a.session,7);
assert.equal(a.flags,3);assert.equal(a.confidence,1);assert.equal(a.ratio,.4);
assert.deepEqual(parseLine('unrelated log'),[]);assert.throws(()=>parseLine('MC1:0011'));
assert.equal(parseLine('MC1:'+Buffer.from(p).toString('hex')+'\x1b[0m').length,1);
const records=[];for(let i=0;i<3750;i++){const q=p.slice();new DataView(q.buffer).setUint32(0,1000+i*16,true);records.push(q);}
const s=summarize(records);assert.equal(s.bytes,90000);assert.equal(s.beats,3750);assert.equal(s.missingMs,0);assert.equal(s.segments,1);
const meta={format:1,demo:true,tracks:[{track:'synthetic <label>',startFrame:0,endFrame:3750,markers:[]}]};
const blob=pack(meta,records);const round=unpack(await blob.arrayBuffer());assert.deepEqual(round.meta,meta);assert.deepEqual(round.records,records);
assert.throws(()=>unpack(new ArrayBuffer(3)));const truncated=new Uint8Array(await blob.arrayBuffer()).slice(0,-1).buffer;assert.throws(()=>unpack(truncated));
new DataView(records[200].buffer).setUint32(0,1000+200*16+64,true);assert(summarize(records).missingMs>0);
console.log('PASS: firmware fixture decoding, malformed packets, one-minute storage, metadata/binary round trip, timing gaps');
