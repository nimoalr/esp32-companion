export const AUDIO_STEMS=['Drums','Bass','Vocals','Other'];
const crcTable=Uint32Array.from({length:256},(_,n)=>{for(let i=0;i<8;i++)n=(n>>>1)^((n&1)?0xedb88320:0);return n>>>0;});
export function assetCRC(bytes){let c=0xffffffff;for(const b of bytes)c=(c>>>8)^crcTable[(c^b)&255];return (c^0xffffffff)>>>0;}
export function validateFLAC(bytes,frames){
 if(!(bytes instanceof Uint8Array)||bytes.length<42||bytes.length>128000000)throw new Error('Invalid stem audio size');
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
 if(v.getUint32(0)!==0x664c6143||(bytes[4]&127)!==0||v.getUint32(4)%16777216!==34)throw new Error('Expected lossless FLAC stem audio');
 const rate=(bytes[18]<<12)|(bytes[19]<<4)|(bytes[20]>>>4),channels=((bytes[20]>>>1)&7)+1,bits=(((bytes[20]&1)<<4)|(bytes[21]>>>4))+1;
 const samples=(bytes[21]&15)*4294967296+v.getUint32(22);
 if(rate!==16000||channels!==2||bits!==16||samples!==frames)throw new Error('Stem audio does not match this microphone timeline');
 return bytes;
}
export function validateStemAudio(audio,reference){
 if(!reference||audio?.sourceSHA256!==reference.source.wavSHA256)throw new Error('Embedded stems belong to another recording');
 for(const name of AUDIO_STEMS)validateFLAC(audio.stems?.[name],reference.source.frames);
 return audio;
}
