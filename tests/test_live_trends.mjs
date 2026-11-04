// Run: node tests/test_live_trends.mjs
// No network access; tests actual embedded HTML JavaScript with a Canvas mock.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source=fs.readFileSync('src/usb_composite/sensor_web.c','utf8');
const first=source.indexOf('    "<script>\\n"'),last=source.indexOf('    "</script>\\n"');
assert.ok(first>=0&&last>first,'embedded Dashboard script not found');
const script=source.slice(first+'    "<script>\\n"'.length,last)
  .trim().split('\n').filter(line=>line.startsWith('    "'))
  .map(line=>JSON.parse(line.trim())).join('');
const noRefresh=script.replace('\nrefresh();\n','\n');
assert.notEqual(noRefresh,script,'refresh entrypoint not found');

function environment(saved={}){
  const plots=new Map(),els=new Map(),store={...saved};
  function canvas(name){
    const ops=[],ctx={
      setTransform(){},clearRect(){ops.push('clear')},beginPath(){},stroke(){},
      fill(){},moveTo(){ops.push('move')},lineTo(){ops.push('line')},
      arc(){ops.push('dot')},fillText(){ops.push('label')},
      drawImage(){ops.push('shift')}
    };
    let width=0,height=0,resizes=0;
    return {name,ops,clientWidth:320,
      get width(){return width},set width(n){width=n;resizes++},
      get height(){return height},set height(n){height=n;resizes++},
      get resizes(){return resizes},getContext(){return ctx}
    };
  }
  for(const id of ['plot-temp','plot-rh','plot-lux','plot-voc']){
    plots.set(id,canvas(id));plots.set(id+'-data',canvas(id+'-data'));
  }
  const document={
    getElementById(id){
      if(plots.has(id))return plots.get(id);
      if(!els.has(id))els.set(id,{textContent:'',dataset:{}});
      return els.get(id);
    },
    createElement(type){assert.equal(type,'canvas');return canvas('scratch')}
  };
  const context=vm.createContext({document,
    window:{devicePixelRatio:1,addEventListener(){}},
    sessionStorage:{getItem(k){return store[k]||null},setItem(k,v){store[k]=v}},
    performance:{now:()=>1000},setTimeout(){},clearTimeout(){},
    AbortController:class{},fetch:async()=>{},console
  });
  vm.runInContext(noRefresh+
    '\nglobalThis.hooks={record,restoreRecent,renderTrends,history,trends,saveRecent,getEpoch:()=>epochStart};',
    context,{timeout:5000});
  return {plots,store,...context.hooks};
}
const mk=(seq,time,good=true)=>({
  sequence:seq,uptime_ms:time,
  bme280:{ok:good,temperature_centi_c:2700+seq%8,humidity_centi_rh:6000+seq%8},
  ltr390:{ok:good,lux_centi:21000+seq%25},
  sgp40:{ok:good,index_ready:good,voc_index:80+seq%5}
});
const env=environment();
const base=env.plots.get('plot-temp'),data=env.plots.get('plot-temp-data');
assert.equal(env.record(mk(1,100000)),true);
env.renderTrends();
assert.ok(data.ops.includes('dot'),'first sample should be visible');
const axCount=base.ops.filter(x=>x==='clear').length;
const dimCount=base.resizes;
assert.equal(dimCount,2);

assert.equal(env.record(mk(2,101000)),true);
env.renderTrends();
assert.ok(data.ops.includes('line'),'line appended to incremental layer');
assert.equal(base.resizes,dimCount,'Canvas backing must not resize per sample');
assert.equal(base.ops.filter(x=>x==='clear').length,axCount,
  'static grid must not redraw for a normal sample');
assert.equal(env.history.length,2);
assert.equal(env.record(mk(2,101000)),false,'duplicate rejected');

// Refresh resumes the same samples in a new JS/Canvas instance.
const resumed=environment(env.store);
assert.equal(resumed.restoreRecent(mk(3,102000)),true,'tab cache accepted');
assert.equal(resumed.history.length,2);
assert.equal(resumed.record(mk(3,102000)),true);
resumed.renderTrends();
assert.equal(resumed.history.length,3);

// Simulate 1 Hz sampling beyond 5m and verify a scroll copies pixels.
for(let seq=4;seq<=304;seq++){
  resumed.record(mk(seq,99000+seq*1000));
  resumed.renderTrends();
}
assert.ok(resumed.history.length<=301,'five-minute bounded history');
const sliding=resumed.plots.get('plot-temp-data').ops;
assert.ok(sliding.includes('shift'),'pixel layer must scroll after five minutes');
assert.equal(resumed.getEpoch(),100000,'session anchor must remain stable');

// Reboot must reset visual history and stored epoch.
assert.equal(resumed.record(mk(1,1000)),true);
assert.equal(resumed.history.length,1);
assert.equal(resumed.getEpoch(),1000);
assert.equal(resumed.trends[0].axis,null);
assert.equal(resumed.trends[0].drawn,null);

// Invalid values create a visible gap rather than interpolated data.
resumed.renderTrends();
resumed.record(mk(2,2000,false));resumed.renderTrends();
const linesBefore=resumed.plots.get('plot-temp-data').ops.filter(x=>x==='line').length;
resumed.record(mk(3,3000,true));resumed.renderTrends();
const linesAfter=resumed.plots.get('plot-temp-data').ops.filter(x=>x==='line').length;
assert.equal(linesAfter,linesBefore,'invalid sample must not be connected');

// A device reboot rejects the former session's saved history.
const other=environment(env.store);
assert.equal(other.restoreRecent(mk(1,1000)),false);
console.log('live strip chart scroll, incremental grid, cache, gaps and reboot PASS');
