// Regression tests for the embedded live Dashboard script.
// Run from repository root: node tests/test_live_trends.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source=fs.readFileSync('src/usb_composite/sensor_web.c','utf8');
const first=source.indexOf('    "<script>\\n"');
const last=source.indexOf('    "</script>\\n"');
assert.ok(first>=0&&last>first,'embedded Dashboard script not found');
const script=source.slice(first+'    "<script>\\n"'.length,last)
  .trim().split('\n').filter(line=>line.startsWith('    "'))
  .map(line=>JSON.parse(line.trim())).join('');
assert.ok(script.includes('function renderTrends()'));
const testScript=script.replace('\nrefresh();\n','\n');
assert.notEqual(testScript,script,'auto-refresh call not found');

const plots=new Map();
for(const id of ['plot-temp','plot-rh','plot-lux','plot-voc']){
  const operations=[];
  const ctx={
    setTransform(){},clearRect(){},beginPath(){},stroke(){},fill(){},
    moveTo(x,y){operations.push(['moveTo',x,y])},
    lineTo(x,y){operations.push(['lineTo',x,y])},
    arc(x,y){operations.push(['dot',x,y])},
    fillText(label){operations.push(['label',label])}
  };
  let width=0,height=0,resizes=0;
  const canvas={
    clientWidth:320,
    get width(){return width},set width(v){width=v;resizes++},
    get height(){return height},set height(v){height=v;resizes++},
    get resizeCount(){return resizes},
    getContext(){return ctx}
  };
  plots.set(id,{canvas,operations});
}
const document={
  getElementById(id){return plots.get(id)?.canvas}
};
const context=vm.createContext({
  document,window:{devicePixelRatio:1,addEventListener(){}},
  performance:{now:()=>1000},
  setTimeout(){},clearTimeout(){},AbortController:class{},fetch:async()=>{}
});
vm.runInContext(testScript+'\nglobalThis.testHooks={record,renderTrends,history,trends,getEpoch:()=>epochStart};',
  context,{timeout:5000});
const {record,renderTrends,history,trends,getEpoch}=context.testHooks;
const mk=(seq,time,ok=true)=>({
  sequence:seq,uptime_ms:time,
  bme280:{ok,temperature_centi_c:2700+seq%8,humidity_centi_rh:6000+seq%8},
  ltr390:{ok,lux_centi:21000+seq%25},
  sgp40:{ok,index_ready:ok,voc_index:80+seq%5}
});
const ops=plots.get('plot-temp').operations;
const lastDot=()=>ops.filter(o=>o[0]==='dot').at(-1);
const hasLabel=v=>ops.some(o=>o[0]==='label'&&o[1]===v);

assert.equal(record(mk(1,100000)),true);
renderTrends();
assert.equal(lastDot()[1],44,'first point must start at left');
assert.ok(hasLabel('start')&&hasLabel('+5 min'),'initial time labels');
const initialResizes=plots.get('plot-temp').canvas.resizeCount;
assert.equal(initialResizes,2);

assert.equal(record(mk(2,101000)),true);
renderTrends();
assert.ok(lastDot()[1]>44,'new sample must extend to the right');
assert.equal(plots.get('plot-temp').canvas.resizeCount,initialResizes,
  'canvas size must not be reset on each sample');
assert.equal(history.length,2);
assert.equal(record(mk(2,101000)),false,'duplicate sample should be ignored');

for(let seq=3;seq<=307;seq++)record(mk(seq,99000+seq*1000));
renderTrends();
assert.ok(history.length<=301,'history must remain within five-minute cap');
assert.equal(getEpoch(),100000,'initial time anchor must remain stable');
assert.ok(hasLabel('-5 min')&&hasLabel('now'),'rolling time labels');
assert.ok(Math.abs(lastDot()[1]-310)<0.001,'newest sample at right edge');

record(mk(1,1000)); // simulated RP2350 reboot
assert.equal(history.length,1,'reboot must clear old samples');
assert.equal(getEpoch(),1000,'reboot must reset time anchor');
assert.equal(trends[0].axis,null,'reboot must clear previous Y-axis range');

record(mk(2,2000,false));
record(mk(3,3000));
ops.length=0;
renderTrends();
assert.equal(ops.filter(o=>o[0]==='lineTo').length,3,
  'invalid values must interrupt the series, leaving only three grid lines');
assert.ok(hasLabel('start'),'rebooted plot must restart at left');
console.log('live trends continuity, canvas stability, gaps, rollover and reboot PASS');
