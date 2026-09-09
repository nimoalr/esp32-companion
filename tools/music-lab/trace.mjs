import {AUDIO_STEMS,assetCRC,validateStemAudio} from './stem-audio.mjs?v=portable4';
export const RECORD_BYTES=64;
export const PCM_RECORD_BYTES=1092;
export const BAND_EDGES_HZ=[1,2,3,4,5,6,7,9,12,16,21,28,38,51,68,91,128].map(x=>x*62.5);
const bf=new DataView(new ArrayBuffer(4));
function power16(n){bf.setUint32(0,n<<16);return bf.getFloat32(0);}
export function decode(p){
 if(![24,64,PCM_RECORD_BYTES].includes(p.length))throw new Error('Invalid feature record size');
 const v=new DataView(p.buffer,p.byteOffset,p.byteLength),hasSpectrum=p.length>=64&&v.getUint16(30,true)!==65535;
 return {ms:v.getUint32(0,true),rms:v.getUint16(4,true),kick:v.getUint16(6,true)/4,
 mean:v.getUint16(8,true)/4,previous:v.getUint16(10,true)/4,bpm:v.getUint16(12,true)/10,
 presence:p[14]/255,ratio:p[15]/127.5,confidence:p[16]/255,depth:p[17]/64,
 flags:p[18],bass:p[19]/255,mid:p[20]/255,high:p[21]/255,session:v.getUint16(22,true),
 hasAudio:p.length===PCM_RECORD_BYTES&&p[64]===80&&p[65]===67&&p[66]===77&&p[67]===49,
 sequence:hasSpectrum?v.getUint32(24,true):null,cpuUs:hasSpectrum?v.getUint16(28,true):null,
 peak:hasSpectrum?v.getUint16(30,true):null,power:hasSpectrum?Array.from({length:16},(_,i)=>power16(v.getUint16(32+2*i,true))):null};
}
export function crc32(p){let c=0xffffffff;for(const b of p){c^=b;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (~c)>>>0;}
export function parseLine(line){
 const match=line.match(/MC([123]):([0-9a-f]+)(?::([0-9a-f]{8}))?(?:\x1b\[[0-9;]*m)?\s*$/i);
 if(!match){if(/MC[123]:/.test(line))throw new Error('Damaged feature packet');return [];}
 const bytes=match[1]==='3'?PCM_RECORD_BYTES:match[1]==='2'?64:24,s=match[2];if(s.length%(bytes*2))throw new Error('Incomplete trace packet');
 const raw=Uint8Array.from(s.match(/../g),s=>parseInt(s,16));
 if(bytes>=64&&(!match[3]||crc32(raw)!==parseInt(match[3],16)))throw new Error('Feature checksum mismatch');
 const out=[];for(let i=0;i<raw.length;i+=bytes)out.push(raw.slice(i,i+bytes));return out;
}
export function pack(meta,records){
 const bytes=records.some(p=>p.length===PCM_RECORD_BYTES)?PCM_RECORD_BYTES:records.some(p=>p.length===64)?64:24;
 const assets=[];let assetBytes=0;
 const tracks=meta.tracks?.map(t=>{
  if(!t.stemAudio)return t;
  validateStemAudio(t.stemAudio,t.stemReference);
  const stems={};for(const name of AUDIO_STEMS){const data=t.stemAudio.stems[name];stems[name]={offset:assetBytes,length:data.length,crc32:assetCRC(data)};assets.push(data);assetBytes+=data.length;}
  return {...t,stemAudio:{sourceSHA256:t.stemAudio.sourceSHA256,stems}};
 });
 const format=assets.length?4:bytes===PCM_RECORD_BYTES?3:bytes===64?2:1;
 const json=new TextEncoder().encode(JSON.stringify({...meta,...(tracks?{tracks}:{}),format,recordBytes:bytes}));
 const header=new Uint8Array(format===4?24:12);header.set(new TextEncoder().encode(`MCALv00${format}`));
 const hv=new DataView(header.buffer);hv.setUint32(8,json.length,true);
 if(records.length>500000||json.length>16000000||header.length+json.length+records.length*bytes+assetBytes>1000000000)throw new Error('Session exceeds portable notebook limits (1 GB / 500,000 frames)');
 if(format===4){hv.setUint32(12,bytes,true);hv.setUint32(16,records.length,true);hv.setUint32(20,assetBytes,true);}
 const normalized=records.map(p=>{if(p.length===bytes)return p;if(![24,64].includes(p.length))throw new Error('Invalid record');const q=new Uint8Array(bytes);q.set(p);if(p.length===24)q.fill(255,24,32);return q;});
 return new Blob([header,json,...normalized,...assets],{type:'application/octet-stream'});
}
export function unpack(buffer){
 const p=new Uint8Array(buffer),v=new DataView(buffer),magic=new TextDecoder().decode(p.slice(0,8));
 if(p.length<12||!['MCALv001','MCALv002','MCALv003','MCALv004'].includes(magic))throw new Error('Not a Music Lab session');
 const portable=magic==='MCALv004',header=portable?24:12;
 if(p.length<header)throw new Error('Incomplete session header');
 const bytes=portable?v.getUint32(12,true):magic==='MCALv003'?PCM_RECORD_BYTES:magic==='MCALv002'?64:24,n=v.getUint32(8,true);
 if(![24,64,PCM_RECORD_BYTES].includes(bytes)||n>16000000||n>p.length-header||p.length>1000000000)throw new Error('Invalid session size');
 const count=portable?v.getUint32(16,true):(p.length-header-n)/bytes,assetBytes=portable?v.getUint32(20,true):0;
 const recordEnd=header+n+count*bytes;
 if(!Number.isInteger(count)||count>500000||recordEnd+assetBytes!==p.length)throw new Error('Incomplete session or size limit exceeded');
 const meta=JSON.parse(new TextDecoder().decode(p.subarray(header,header+n)));
 let consumed=0;
 for(const track of meta.tracks||[]){
  if(!track.stemAudio)continue;
  if(!portable)throw new Error('Embedded audio requires a version 4 notebook');
  const stems={};for(const name of AUDIO_STEMS){const d=track.stemAudio.stems?.[name];
   if(!d||!Number.isInteger(d.offset)||!Number.isInteger(d.length)||d.offset!==consumed||d.length<42||d.length>assetBytes-consumed||!Number.isInteger(d.crc32))throw new Error('Invalid stem attachment bounds');
   const data=p.subarray(recordEnd+d.offset,recordEnd+d.offset+d.length);
   if(assetCRC(data)!==d.crc32)throw new Error('Damaged stem audio attachment');
   stems[name]=data;consumed+=d.length;
  }
  track.stemAudio=validateStemAudio({sourceSHA256:track.stemAudio.sourceSHA256,stems},track.stemReference);
 }
 if(consumed!==assetBytes)throw new Error('Unreferenced stem attachment data');
 const records=[];for(let i=header+n;i<recordEnd;i+=bytes)records.push(p.subarray(i,i+bytes));return {meta,records};
}
export function summarize(records){
 let danced=0,listened=0,beats=0,candidates=0,clipped=0,eligible=0,rush=0,missingMs=0,segments=0,prev=null,droppedFrames=0,sequenceFrames=0;
 let audioFrames=0;
 let min=Infinity,max=0,sum=0,ms=0,bytes=0,cpuMaxUs=0,spectralFrames=0;const powerSum=Array(16).fill(0);
 for(const p of records){const a=decode(p);bytes+=p.length;audioFrames+=a.hasAudio;danced+=!!(a.flags&64);listened+=!!(a.flags&128);beats+=!!(a.flags&2);candidates+=!!(a.flags&1);clipped+=!!(a.flags&16);rush+=!!(a.flags&32);
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
 return {frames:n,bytes,audioFrames,seconds:ms/1000,sampledSeconds:n*.016,featureHz:ms&&segments===1?(n-1)*1000/ms:null,beats,candidates,rush,clippedFrames:clipped,
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
