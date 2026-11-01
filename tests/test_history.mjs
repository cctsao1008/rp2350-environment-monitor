// Real protected History page embedded JS contract tests, without network.
// Run: node tests/test_history.mjs /tmp/envlog_history.js
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const script=fs.readFileSync(process.argv[2]||'/tmp/envlog_history.js','utf8');
const SECTOR=4096, PAGE=256, manifestCallsPerScan=48;
const payload=new Uint8Array(SECTOR).fill(0xff);
const set16=(p,n)=>{payload[p]=n&255;payload[p+1]=(n>>8)&255};
const set32=(p,n)=>{for(let i=0;i<4;i++)payload[p+i]=(n>>>(8*i))&255};
function crc32(b,from,to,omitFrom=-1,omitTo=-1){
 let c=0xffffffff;
 for(let i=from;i<to;i++){
  if(i>=omitFrom&&i<omitTo)continue;
  c^=b[i];for(let j=0;j<8;j++)c=(c>>>1)^((c&1)?0xedb88320:0);
 }
 return (c^0xffffffff)>>>0;
}
payload.set([69,76,79,71],0);
set16(4,1);set16(6,256);set32(8,1);set32(12,7);
set32(16,4194304);set32(20,12582912);
set16(24,24);set16(26,256);set16(28,4096);set16(30,10);
set32(252,crc32(payload,0,252));
const headerCRC=(payload[252]|(payload[253]<<8)|(payload[254]<<16)|(payload[255]<<24))>>>0;
for(let k=0;k<2;k++){
 const pos=PAGE*(k+1),time=3000000+k*10000000;
 set32(pos,time);set32(pos+4,0);set32(pos+8,1+k*10);
 for(let i=0;i<10;i++){
   let at=pos+16+i*24;
   set16(at,i*1000);
   set16(at+2,2500+k*40+i);set16(at+4,6300+i);
   payload[at+6]=0xe8;payload[at+7]=0x86;payload[at+8]=0x01;
   set16(at+9,2000+i);set16(at+11,300);
   payload[at+13]=120;payload[at+16]=0;
   set16(at+19,29500+i);
   set16(at+21,100+k*3+i);
   payload[at+23]=0x2f; // BME+TSL+LTR+SGP+VOC_ready
 }
 set32(pos+12,crc32(payload,pos,pos+PAGE,pos+12,pos+16));
}

async function scenario(kind){
 let clicked, manifestCalls=0,elementMap=new Map(),chartDraws=0;
 const ctx={
  clearRect(){},setTransform(){},fillText(){},beginPath(){},moveTo(){},
  lineTo(){},stroke(){},set font(v){},set fillStyle(v){},
  set strokeStyle(v){},set lineWidth(v){}
 };
 for(const id of [
  'load','session','metric','progress','message','samples','sectors',
  'sessions','warnings','plot','session-rows','chart-info'
 ]){
  const node={
   id,disabled:id==='session'||id==='metric',textContent:'',
   value:id==='metric'?'temperature':'',max:1,clientWidth:860,
   children:[],
   addEventListener(type,fn){if(id==='load'&&type==='click')clicked=fn},
   appendChild(c){this.children.push(c)},
   getContext(type){assert.equal(type,'2d');chartDraws++;return ctx}
  };
  elementMap.set(id,node);
 }
 const document={
  getElementById:id=>elementMap.get(id),
  createElement(tag){return {tag,value:'',textContent:'',children:[],
    appendChild(child){this.children.push(child)}}}
 };
 const window={addEventListener(){}};
 async function fetch(path,opts){
  assert.equal(opts.redirect,'error');
  if(path==='/api/v1/log/status')return {ok:true,json:async()=>({
    schema:'rp2350.envlog.export.v1',
    sectorCount:3072,sectorBytes:4096,partitionBytes:12582912,
    manifestPage:64,unknownHeaders:0,validSectors:1})};
  if(path.startsWith('/api/v1/log/list/')){
   const start=Number(path.slice('/api/v1/log/list/'.length));
   assert.equal(path,'/api/v1/log/list/'+start);
   manifestCalls++;
   const corruptGeneration=kind==='recycled' && manifestCalls>48;
   return {ok:true,json:async()=>({schema:'rp2350.envlog.list.v1',start,
    entries:start===0?[[0,corruptGeneration?2:1,headerCRC]]:[]})};
  }
  if(path==='/api/v1/log/sector/0/1/'+headerCRC){
   const clone=payload.slice();
   if(kind==='crc')clone[256+16+2]^=0x80;
   return {ok:true,arrayBuffer:async()=>clone.buffer};
  }
  throw Error('Unexpected network URL '+path);
 }
 const context={window,document,fetch,Uint8Array,BigInt,Number,Map,Set,
    Math,Error,Promise,Date,devicePixelRatio:1,console};
 vm.runInNewContext(script,context,{timeout:5000});
 assert.ok(clicked,'History Load event must be attached');
 await clicked();
 assert.equal(manifestCalls,kind==='crc'?48:manifestCallsPerScan*2);
 const message=elementMap.get('message').textContent;
 if(kind==='good'){
  assert.equal(elementMap.get('samples').textContent,'20');
  assert.equal(elementMap.get('sessions').textContent,'1');
  assert.equal(elementMap.get('warnings').textContent,'0');
  assert.equal(elementMap.get('session').value,'7');
  assert.ok(chartDraws>0);
  assert.match(message,/History ready: 20 CRC-valid records/);
 }else{
  assert.match(message,/History load aborted/);
  assert.equal(elementMap.get('samples').textContent,'—');
  if(kind==='crc')assert.match(message,/Page CRC mismatch/);
  if(kind==='recycled')assert.match(message,/Flash ring recycled/);
 }
}
await scenario('good');
await scenario('crc');
await scenario('recycled');
console.log('history chart browser validation and race tests PASS');
