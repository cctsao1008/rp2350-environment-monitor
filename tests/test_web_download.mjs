// Run after CI extracts the actual browser script from log_web_page.h.
// This is an end-to-end mocked HTTP contract test: no network or RP2350.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

const SCRIPT=fs.readFileSync(process.argv[2]||'/tmp/envlog_web.js','utf8');
const SIZE=4096, SLOTS=3072;
const sector=new Uint8Array(SIZE).fill(0xff);
sector.set([69,76,79,71,1,0,0,1],0); // ELOG, v1, header length 256
const write32=(off,n)=>{for(let i=0;i<4;i++)sector[off+i]=(n>>>(8*i))&255};
const crc32=(arr,n)=>{let crc=0xffffffff;for(let i=0;i<n;i++){
  crc^=arr[i];for(let j=0;j<8;j++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);
}return (crc^0xffffffff)>>>0};
write32(8,1);write32(12,10);write32(16,4194304);write32(20,12582912);
sector.set([24,0,0,1,0,16,10,0],24);
const crc=crc32(sector,252);write32(252,crc);

async function scenario(mutate) {
  let clicks=0, emittedBlob, onClick, manifestCalls=0;
  const els=new Map();
  for(const id of ['download','progress','result','summary']){
    els.set(id,{disabled:true,value:0,max:1,textContent:'',
      addEventListener(event,cb){if(id==='download'&&event==='click')onClick=cb}});
  }
  const document={
    getElementById:id=>els.get(id),
    createElement:tag=>({href:'',download:'',click(){clicks++},remove(){}}),
    body:{append(){}}
  };
  async function fetch(url,options) {
    assert.equal(options.redirect,'error');
    const fail=message=>{throw Error(message)};
    if(url==='/api/v1/log/status'){
      return {ok:true,status:200,json:async()=>({
        schema:'rp2350.envlog.export.v1',partitionBytes:12582912,
        sectorBytes:4096,sectorCount:SLOTS,manifestPage:64,
        validSectors:1,unknownHeaders:0,version:1})};
    }
    if(url.startsWith('/api/v1/log/list?start=')){
      manifestCalls++;
      const start=Number(url.split('=')[1]);
      const mutated=mutate&&manifestCalls>48;
      return {ok:true,status:200,json:async()=>({
        schema:'rp2350.envlog.list.v1',start,
        entries:start===0?[[0,mutated?2:1,crc]]:[]})};
    }
    if(url.startsWith('/api/v1/log/sector?')){
      return {ok:true,status:200,
        arrayBuffer:async()=>Uint8Array.from(sector).buffer};
    }
    fail('Unexpected route '+url);
  }
  const context={document,fetch,Blob,Uint8Array,Number,Date,Map,Set,
    Math,Error,URL:{createObjectURL:b=>{emittedBlob=b;return 'blob:mock'},
      revokeObjectURL(){}},setTimeout:()=>0,console};
  vm.runInNewContext(SCRIPT,context);
  await new Promise(r=>setImmediate(r));
  assert.equal(els.get('download').disabled,false);
  await onClick();
  assert.equal(els.get('download').disabled,false);
  assert.equal(manifestCalls,96,'Initial and final manifest must each scan 48 pages');
  if(mutate){
    assert.equal(clicks,0,'Changed sector generation must abort download');
    assert.match(els.get('result').textContent,/aborted/i);
  }else{
    assert.equal(clicks,1,'One completed browser download');
    assert.ok(emittedBlob);
    const out=new Uint8Array(await emittedBlob.arrayBuffer());
    assert.equal(out.length,12582912);
    assert.deepEqual(out.subarray(0,256),sector.subarray(0,256));
    assert.equal(out[4096],255);
    assert.match(els.get('result').textContent,/Downloaded 1 sectors/);
  }
}
await scenario(false);
await scenario(true);
console.log('browser binary export simulated integration PASS');
