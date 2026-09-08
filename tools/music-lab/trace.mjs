export const RECORD_BYTES=64;
export const BAND_EDGES_HZ=[1,2,3,4,5,6,7,9,12,16,21,28,38,51,68,91,128].map(x=>x*62.5);
const bf=new DataView(new ArrayBuffer(4));
function power16(n){bf.setUint32(0,n<<16);return bf.getFloat32(0);}
export function decode(p){
 if(p.length!==24&&p.length!==64)throw new Error('Invalid feature record size');
 const v=new DataView(p.buffer,p.byteOffset,p.byteLength),hasSpectrum=p.length===64&&v.getUint16(30,true)!==65535;
 return {ms:v.getUint32(0,true),rms:v.getUint16(4,true),kick:v.getUint16(6,true)/4,
 mean:v.getUint16(8,true)/4,previous:v.getUint16(10,true)/4,bpm:v.getUint16(12,true)/10,
 presence:p[14]/255,ratio:p[15]/127.5,confidence:p[16]/255,depth:p[17]/64,
 flags:p[18],bass:p[19]/255,mid:p[20]/255,high:p[21]/255,session:v.getUint16(22,true),
 sequence:hasSpectrum?v.getUint32(24,true):null,cpuUs:hasSpectrum?v.getUint16(28,true):null,
 peak:hasSpectrum?v.getUint16(30,true):null,power:hasSpectrum?Array.from({length:16},(_,i)=>power16(v.getUint16(32+2*i,true))):null};
}
export function crc32(p){let c=0xffffffff;for(const b of p){c^=b;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (~c)>>>0;}
export function parseLine(line){
 const match=line.match(/MC([12]):([0-9a-f]+)(?::([0-9a-f]{8}))?(?:\x1b\[[0-9;]*m)?\s*$/i);
 if(!match){if(/MC[12]:/.test(line))throw new Error('Damaged feature packet');return [];}
 const bytes=match[1]==='2'?64:24,s=match[2];if(s.length%(bytes*2))throw new Error('Incomplete trace packet');
 const raw=Uint8Array.from(s.match(/../g),s=>parseInt(s,16));
 if(bytes===64&&(!match[3]||crc32(raw)!==parseInt(match[3],16)))throw new Error('Feature checksum mismatch');
 const out=[];for(let i=0;i<raw.length;i+=bytes)out.push(raw.slice(i,i+bytes));return out;
}
export function pack(meta,records){
 const bytes=records.some(p=>p.length===64)?64:24;
 const json=new TextEncoder().encode(JSON.stringify({...meta,format:bytes===64?2:1,recordBytes:bytes}));
 const header=new Uint8Array(12);header.set(new TextEncoder().encode(bytes===64?'MCALv002':'MCALv001'));
 new DataView(header.buffer).setUint32(8,json.length,true);
 const normalized=records.map(p=>{if(p.length===bytes)return p;if(p.length!==24)throw new Error('Invalid record');const q=new Uint8Array(64);q.set(p);q.fill(255,24,32);return q;});
 return new Blob([header,json,...normalized],{type:'application/octet-stream'});
}
export function unpack(buffer){
 const p=new Uint8Array(buffer),v=new DataView(buffer),magic=new TextDecoder().decode(p.slice(0,8));
 if(p.length<12||!['MCALv001','MCALv002'].includes(magic))throw new Error('Not a Music Lab session');
 const bytes=magic==='MCALv002'?64:24,n=v.getUint32(8,true);
 if(n>p.length-12||(p.length-12-n)%bytes)throw new Error('Incomplete session');
 const meta=JSON.parse(new TextDecoder().decode(p.slice(12,12+n)));
 const records=[];for(let i=12+n;i<p.length;i+=bytes)records.push(p.slice(i,i+bytes));return {meta,records};
}
export function summarize(records){
 let danced=0,listened=0,beats=0,candidates=0,clipped=0,eligible=0,rush=0,missingMs=0,segments=0,prev=null,droppedFrames=0,sequenceFrames=0;
 let min=Infinity,max=0,sum=0,ms=0,bytes=0,cpuMaxUs=0,spectralFrames=0;const powerSum=Array(16).fill(0);
 for(const p of records){const a=decode(p);bytes+=p.length;danced+=!!(a.flags&64);listened+=!!(a.flags&128);beats+=!!(a.flags&2);candidates+=!!(a.flags&1);clipped+=!!(a.flags&16);rush+=!!(a.flags&32);
  eligible+=a.bpm>=85&&a.bpm<=185&&a.confidence>=.745&&a.ratio>=.08;
  min=Math.min(min,a.rms);max=Math.max(max,a.rms);sum+=a.rms;cpuMaxUs=Math.max(cpuMaxUs,a.cpuUs??0);
  if(a.sequence!==null)sequenceFrames++;
  if(a.power){spectralFrames++;a.power.forEach((v,i)=>powerSum[i]+=Number.isFinite(v)?v:0);}
  if(prev&&a.session===prev.session&&((a.ms-prev.ms)>>>0)<0x80000000){const dt=(a.ms-prev.ms)>>>0;ms+=dt;if(dt>40)missingMs+=dt-16;
   if(a.sequence!==null&&prev.sequence!==null){const gap=(a.sequence-prev.sequence)>>>0;if(gap>1&&gap<0x80000000)droppedFrames+=gap-1;}
  }else segments++;
  prev=a;
 }
 const n=records.length,total=powerSum.reduce((a,b)=>a+b,0);
 return {frames:n,bytes,seconds:ms/1000,sampledSeconds:n*.016,featureHz:ms&&segments===1?(n-1)*1000/ms:null,beats,candidates,rush,clippedFrames:clipped,
  dancePercent:n?100*danced/n:0,listenPercent:n?100*listened/n:0,eligiblePercent:n?100*eligible/n:0,rmsMin:n?min:0,rmsMean:n?sum/n:0,rmsMax:max,missingMs,segments,droppedFrames:sequenceFrames?droppedFrames:null,cpuMaxUs,
  spectrum:spectralFrames?{frames:spectralFrames,edgesHz:BAND_EDGES_HZ,meanPower:powerSum.map(v=>v/spectralFrames),energyPercent:powerSum.map(v=>total?100*v/total:0)}:null};
}
/* Per-track spectra plus two complementary corpus views: equal weight per
 * track, and time/energy weighting. Avoid letting one loud/long track hide gaps. */
export function summarizeCorpus(tracks){
 const usable=tracks.filter(t=>!t.demo&&t.summary?.spectrum),equal=Array(16).fill(0),energy=Array(16).fill(0);
 for(const t of usable){const s=t.summary.spectrum;s.energyPercent.forEach((v,i)=>equal[i]+=v/usable.length);s.meanPower.forEach((v,i)=>energy[i]+=v*s.frames);}
 const total=energy.reduce((a,b)=>a+b,0);
 return {tracks:usable.length,edgesHz:BAND_EDGES_HZ,equalTrackPercent:equal,energyPercent:energy.map(v=>total?v*100/total:0)};
}
