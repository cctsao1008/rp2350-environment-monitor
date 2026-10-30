/*
 * Phase 2 read-only Sensor Web UI for the existing lwIP HTTPD.
 * MIT; application code. HTTP only reads a completed Sensor Manager snapshot.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lwip/apps/fs.h"
#include "lwip/mem.h"
#include "sensor_web.h"

#define SENSOR_JSON_CAPACITY 2048u

static sensor_snapshot_t latest_snapshot;
static bool latest_valid;

/* Entire document is embedded in Flash. No external CSS/JS/CDN required.
 * HTTP/1.0 closes the connection to frame these static header-included files.
 */
static const char dashboard_response[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "Connection: close\r\n\r\n"
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>RP2350 Environment Monitor</title>\n"
    "<style>\n"
    ":root{color-scheme:dark;font:15px system-ui,sans-serif}\n"
    "*{box-sizing:border-box}\n"
    "body{margin:0;background:#0c1624;color:#e8f0f7}\n"
    "main{max-width:1080px;margin:auto;padding:24px 16px}\n"
    "header{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}\n"
    "h1{font-size:1.55rem;margin:0}\n"
    "p{color:#9fb2c5}\n"
    "#state{padding:7px 12px;border:1px solid #415166;border-radius:20px}\n"
    "#state[data-state=\"Fresh\"]{border-color:#3e866f;color:#a5e6cb}\n"
    "#state[data-state=\"Stale\"]{border-color:#997b3d;color:#f4d999}\n"
    "#state[data-state=\"Disconnected\"]{border-color:#a65454;color:#f5b9b9}\n"
    ".grid,.trends{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px;margin-top:20px}\n"
    "article,figure{background:#17283b;border:1px solid #294058;border-radius:14px;padding:17px;margin:0}\n"
    "h2{font-size:1.05rem;margin:0 0 10px}\n"
    ".value{font-size:1.28rem;font-weight:700;margin:8px 0;word-break:break-word}\n"
    ".secondary,.quality{font-size:.85rem;color:#9fb2c5;line-height:1.6}\n"
    ".quality{display:inline-block;margin-bottom:3px}\n"
    ".quality[data-status=\"bad\"]{color:#ffb5b5}\n"
    ".quality[data-status=\"warn\"]{color:#f4d999}\n"
    ".meta{margin-top:20px;font-size:.85rem;color:#9fb2c5}\n"
    "a{color:#86c9f5}\n"
    "section>h2{margin-top:26px}\n"
    "figure figcaption{font-weight:600;margin-bottom:10px}\n"
    "canvas{width:100%;height:132px;display:block}\n"
    "</style>\n"
    "</head>\n"
    "<body><main>\n"
    "<header>\n"
    " <div><h1>RP2350 Environment Monitor</h1><p>Sensor telemetry over USB CDC-NCM</p></div>\n"
    " <span id=\"state\" data-state=\"Connecting\" role=\"status\">Connecting</span>\n"
    "</header>\n"
    "<section class=\"grid\" aria-label=\"Latest sensor readings\">\n"
    " <article><h2>BME280 &middot; Environment</h2><div class=\"quality\" id=\"bme-status\">Waiting</div>\n"
    " <div class=\"value\" id=\"bme\">--</div><div class=\"secondary\">Temperature / RH / pressure</div></article>\n"
    " <article><h2>TSL2591 &middot; Light</h2><div class=\"quality\" id=\"tsl-status\">Waiting</div>\n"
    " <div class=\"value\" id=\"tsl\">--</div><div class=\"secondary\">Estimated lux; saturation is invalid</div></article>\n"
    " <article><h2>LTR390 &middot; UV / ALS</h2><div class=\"quality\" id=\"ltr-status\">Waiting</div>\n"
    " <div class=\"value\" id=\"ltr\">--</div><div class=\"secondary\">Estimated UV index and ambient lux</div></article>\n"
    " <article><h2>SGP40 &middot; VOC</h2><div class=\"quality\" id=\"sgp-status\">Waiting</div>\n"
    " <div class=\"value\" id=\"sgp\">--</div><div class=\"secondary\">VOC Index, not ppm or concentration</div></article>\n"
    " <article><h2>ICM20948 &middot; Motion</h2><div class=\"quality\" id=\"imu-status\">Waiting</div>\n"
    " <div class=\"value\" id=\"imu\">--</div><div class=\"secondary\">Accel g / gyro deg/s, XYZ device axes</div></article>\n"
    " <article><h2>AK09916 &middot; Magnetic field</h2><div class=\"quality\" id=\"mag-status\">Waiting</div>\n"
    " <div class=\"value\" id=\"mag\">--</div><div class=\"secondary\">Uncalibrated XYZ magnetic field</div></article>\n"
    "</section>\n"
    "<section aria-label=\"Five-minute live trends\">\n"
    " <h2>Live trends &middot; last 5 minutes</h2>\n"
    " <div class=\"secondary\">Browser memory only; plots update for new 1 Hz sample sequences and skip invalid values.</div>\n"
    " <div class=\"trends\">\n"
    "  <figure><figcaption>Temperature &middot; &deg;C</figcaption>\n"
    "   <canvas id=\"plot-temp\" height=\"132\" role=\"img\" aria-label=\"Five-minute temperature trend\"></canvas></figure>\n"
    "  <figure><figcaption>Humidity &middot; %RH</figcaption>\n"
    "   <canvas id=\"plot-rh\" height=\"132\" role=\"img\" aria-label=\"Five-minute relative humidity trend\"></canvas></figure>\n"
    "  <figure><figcaption>Ambient light &middot; lux</figcaption>\n"
    "   <canvas id=\"plot-lux\" height=\"132\" role=\"img\" aria-label=\"Five-minute LTR390 light trend\"></canvas></figure>\n"
    "  <figure><figcaption>VOC Index</figcaption>\n"
    "   <canvas id=\"plot-voc\" height=\"132\" role=\"img\" aria-label=\"Five-minute VOC Index trend\"></canvas></figure>\n"
    " </div>\n"
    "</section>\n"
    "<p class=\"meta\" id=\"meta\">Waiting for first completed sample.</p>\n"
    "<p class=\"meta\">Read-only local USB interface &middot;\n"
    "<a href=\"/api/v1/sensors.json\">Sensor JSON API</a></p>\n"
    "</main>\n"
    "<script>\n"
    "'use strict';\n"
    "const el=id=>document.getElementById(id);\n"
    "const fmt=(n,d)=>Number(n).toFixed(d);\n"
    "const valid=n=>typeof n==='number'&&Number.isFinite(n);\n"
    "const xyz=(values,unit,scale,digits)=>Array.isArray(values)&&values.length===3\n"
    "  ?values.map(v=>fmt(v/scale,digits)).join(', ')+' '+unit:'Unavailable';\n"
    "const WINDOW_MS=300000,STALE_MS=4500;\n"
    "const history=[];\n"
    "let lastSequence=null,lastUptime=null,lastNewAt=0;\n"
    "const trends=[\n"
    " {id:'plot-temp',key:'temp',color:'#78c8ff'},\n"
    " {id:'plot-rh',key:'rh',color:'#b1e6a8'},\n"
    " {id:'plot-lux',key:'lux',color:'#f7d589'},\n"
    " {id:'plot-voc',key:'voc',color:'#efa8d7'}\n"
    "];\n"
    "function setState(state){\n"
    " const node=el('state');node.textContent=state;node.dataset.state=state;\n"
    "}\n"
    "function setMetric(id,label,value,status){\n"
    " el(id).textContent=value;\n"
    " const n=el(id+'-status');n.textContent=label;n.dataset.status=status;\n"
    "}\n"
    "function updateReadings(s){\n"
    " const b=s.bme280||{};\n"
    " setMetric('bme',b.ok?'VALID':'READ ERROR',b.ok\n"
    "   ?fmt(b.temperature_centi_c/100,2)+' C / '+fmt(b.humidity_centi_rh/100,2)\n"
    "    +'% / '+fmt(b.pressure_pa/100,2)+' hPa':'Unavailable',b.ok?'good':'bad');\n"
    " const t=s.tsl2591||{};\n"
    " const saturated=t.ok&&t.saturated;\n"
    " setMetric('tsl',!t.ok?'READ ERROR':saturated?'SATURATED':'VALID',\n"
    "   !t.ok?'Unavailable':saturated?'Invalid (saturated)':fmt(t.lux_centi/100,2)+' lx',\n"
    "   !t.ok?'bad':saturated?'warn':'good');\n"
    " const l=s.ltr390||{};\n"
    " setMetric('ltr',l.ok?'VALID':'NOT READY / ERROR',\n"
    "   l.ok?fmt(l.uvi_centi/100,2)+' UVI / '+fmt(l.lux_centi/100,2)+' lx':'Unavailable',\n"
    "   l.ok?'good':'bad');\n"
    " const g=s.sgp40||{};\n"
    " const warming=g.ok&&!g.index_ready;\n"
    " setMetric('sgp',!g.ok?'NOT READY / COMPENSATION ERROR':warming?'WARMING UP':'VALID',\n"
    "   !g.ok?'Unavailable':warming?'Warming up':String(g.voc_index),\n"
    "   !g.ok?'bad':warming?'warn':'good');\n"
    " const i=s.icm20948||{};\n"
    " setMetric('imu',i.ok?'VALID':'READ ERROR',\n"
    "   i.ok?xyz(i.accel_mg,'g',1000,3)+' / '+xyz(i.gyro_mdps,'deg/s',1000,3)\n"
    "   :'Unavailable',i.ok?'good':'bad');\n"
    " const m=s.ak09916||{};\n"
    " const overflow=m.reason==='overflow';\n"
    " setMetric('mag',m.ok?'UNCALIBRATED':overflow?'OVERFLOW':'UNAVAILABLE',\n"
    "   m.ok?xyz(m.mag_centi_ut,'uT',100,2):'Unavailable',\n"
    "   m.ok?'warn':overflow?'bad':'bad');\n"
    " el('meta').textContent='Sample '+s.sequence+' | Sensor uptime '+fmt(s.uptime_ms/1000,1)\n"
    "   +' s | Nominal 1 Hz sampling | Browser history '+history.length+' samples';\n"
    "}\n"
    "function measure(s){\n"
    " const b=s.bme280||{},l=s.ltr390||{},g=s.sgp40||{};\n"
    " return {time:s.uptime_ms,\n"
    "   temp:b.ok&&valid(b.temperature_centi_c)?b.temperature_centi_c/100:null,\n"
    "   rh:b.ok&&valid(b.humidity_centi_rh)?b.humidity_centi_rh/100:null,\n"
    "   lux:l.ok&&valid(l.lux_centi)?l.lux_centi/100:null,\n"
    "   voc:g.ok&&g.index_ready&&valid(g.voc_index)?g.voc_index:null\n"
    " };\n"
    "}\n"
    "function record(s){\n"
    " if(lastSequence!==null&&(s.sequence<lastSequence||s.uptime_ms<lastUptime)){\n"
    "   history.length=0; // device reboot; uptime/sequence epoch restarted\n"
    "   lastSequence=null;\n"
    " }\n"
    " if(s.sequence===lastSequence&&s.uptime_ms===lastUptime)return false;\n"
    " if(lastSequence!==null&&s.sequence<=lastSequence)return false;\n"
    " history.push(measure(s));\n"
    " lastSequence=s.sequence;lastUptime=s.uptime_ms;lastNewAt=performance.now();\n"
    " while(history.length&&\n"
    "       (s.uptime_ms-history[0].time>WINDOW_MS||history.length>301)){\n"
    "   history.shift();\n"
    " }\n"
    " return true;\n"
    "}\n"
    "function renderTrends(){\n"
    " const newest=history.length?history[history.length-1].time:0;\n"
    " for(const cfg of trends){\n"
    "   const canvas=el(cfg.id),width=Math.max(160,Math.round(canvas.clientWidth));\n"
    "   const h=132,dpr=Math.min(window.devicePixelRatio||1,2);\n"
    "   canvas.width=Math.ceil(width*dpr);canvas.height=Math.ceil(h*dpr);\n"
    "   const ctx=canvas.getContext('2d');\n"
    "   ctx.setTransform(dpr,0,0,dpr,0,0);\n"
    "   ctx.clearRect(0,0,width,h);\n"
    "   const pad={left:44,right:10,top:16,bottom:20};\n"
    "   const plotW=width-pad.left-pad.right,plotH=h-pad.top-pad.bottom;\n"
    "   ctx.font='11px system-ui';ctx.lineWidth=1;\n"
    "   const values=history.map(p=>p[cfg.key]).filter(valid);\n"
    "   if(!values.length){\n"
    "     ctx.fillStyle='#9fb2c5';ctx.fillText('No valid samples',pad.left,pad.top+25);\n"
    "     continue;\n"
    "   }\n"
    "   let low=Math.min(...values),high=Math.max(...values);\n"
    "   const margin=Math.max((high-low)*0.12,0.05);\n"
    "   low-=margin;high+=margin;\n"
    "   ctx.strokeStyle='#355066';ctx.fillStyle='#9fb2c5';\n"
    "   for(let j=0;j<3;j++){\n"
    "     const y=pad.top+plotH*j/2;\n"
    "     ctx.beginPath();ctx.moveTo(pad.left,y);ctx.lineTo(width-pad.right,y);ctx.stroke();\n"
    "     ctx.fillText((high-(high-low)*j/2).toFixed(1),3,y+3);\n"
    "   }\n"
    "   ctx.fillText('-5 min',pad.left,h-4);\n"
    "   ctx.fillText('now',width-36,h-4);\n"
    "   const baseline=newest-WINDOW_MS;\n"
    "   ctx.strokeStyle=cfg.color;ctx.lineWidth=2;ctx.beginPath();\n"
    "   let active=false,previous=0;\n"
    "   for(const p of history){\n"
    "     const v=p[cfg.key];\n"
    "     if(!valid(v)){active=false;continue;}\n"
    "     const x=pad.left+Math.max(0,(p.time-baseline)/WINDOW_MS)*plotW;\n"
    "     const y=pad.top+(high-v)/(high-low)*plotH;\n"
    "     if(!active||p.time-previous>3000)ctx.moveTo(x,y);\n"
    "     else ctx.lineTo(x,y);\n"
    "     active=true;previous=p.time;\n"
    "   }\n"
    "   ctx.stroke();\n"
    " }\n"
    "}\n"
    "async function refresh(){\n"
    " const controller=new AbortController();\n"
    " const timer=setTimeout(()=>controller.abort(),4000);\n"
    " try{\n"
    "   const response=await fetch('/api/v1/sensors.json',\n"
    "     {cache:'no-store',signal:controller.signal});\n"
    "   if(!response.ok)throw Error('HTTP '+response.status);\n"
    "   const s=await response.json();\n"
    "   if(!Number.isSafeInteger(s.sequence)||!valid(s.uptime_ms)\n"
    "       ||s.schema!=='rp2350.environment.v1')throw Error('Invalid API schema');\n"
    "   const changed=record(s);\n"
    "   if(changed)renderTrends();\n"
    "   updateReadings(s);\n"
    "   const stale=performance.now()-lastNewAt>STALE_MS;\n"
    "   setState(stale?'Stale':'Fresh');\n"
    "   if(stale)el('meta').textContent+=' | STALE: sample sequence not advancing';\n"
    " }catch(error){\n"
    "   setState('Disconnected');\n"
    "   el('meta').textContent=(lastSequence===null?'No sample received. ':\n"
    "     'Values above are last-known, not live. ')+\n"
    "     'Connection unavailable: '+error.message;\n"
    " }finally{\n"
    "   clearTimeout(timer);\n"
    "   setTimeout(refresh,1000); // serialize requests; no overlapping HTTP polls\n"
    " }\n"
    "}\n"
    "refresh();\n"
    "window.addEventListener('resize',renderTrends);\n"
    "</script>\n"
    "</body>\n"
    "</html>\n";
static const char initializing_response[] =
    "HTTP/1.0 503 Service Unavailable\r\n"
    "Content-Type: application/json\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n\r\n"
    "{\"status\":\"initializing\",\"message\":\"No completed sensor sample yet\"}\n";

static const char resource_response[] =
    "HTTP/1.0 503 Service Unavailable\r\n"
    "Content-Type: application/json\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n\r\n"
    "{\"status\":\"unavailable\",\"message\":\"HTTP response memory unavailable\"}\n";

typedef struct {
    char *text;
    size_t size;
    size_t used;
    bool valid;
} json_writer_t;

static void json_add(json_writer_t *w, const char *fmt, ...) {
    if (!w->valid) return;
    if (w->used >= w->size) {
        w->valid = false;
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(w->text + w->used, w->size - w->used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->size - w->used) {
        w->valid = false;
        return;
    }
    w->used += (size_t)n;
}

void sensor_web_publish_snapshot(const sensor_snapshot_t *snapshot) {
    if (snapshot == NULL) return;
    /* The Sensor Manager, USB task and lwIP all run on one core/thread. */
    latest_snapshot = *snapshot;
    latest_valid = true;
}

static bool url_matches(const char *actual, const char *route) {
    size_t n = strlen(route);
    return strncmp(actual, route, n) == 0 &&
           (actual[n] == '\0' || actual[n] == '?');
}

static void serve_literal(struct fs_file *file, const char *data, size_t bytes) {
    file->data = data;
    file->len = (int)bytes;
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    file->pextension = NULL;
}

static void render_json(json_writer_t *w, const sensor_snapshot_t *s) {
    json_add(w,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n"
        "{\"schema\":\"rp2350.environment.v1\",\"sequence\":%lu,\"uptime_ms\":%lu,",
        (unsigned long)s->sequence, (unsigned long)s->uptime_ms);

    json_add(w, "\"bme280\":{\"ok\":%s", s->bme280_ok ? "true" : "false");
    if (s->bme280_ok) {
        json_add(w, ",\"temperature_centi_c\":%ld,\"humidity_centi_rh\":%lu,\"pressure_pa\":%lu",
            (long)s->bme280.temperature_centi_c,
            (unsigned long)s->bme280.humidity_centi_percent,
            (unsigned long)s->bme280.pressure_pa);
    }
    json_add(w, "},");

    json_add(w, "\"tsl2591\":{\"ok\":%s", s->tsl2591_ok ? "true" : "false");
    if (s->tsl2591_ok) {
        json_add(w, ",\"saturated\":%s,\"full_count\":%u,\"ir_count\":%u",
            s->tsl2591.saturated ? "true" : "false",
            (unsigned)s->tsl2591.full_count, (unsigned)s->tsl2591.ir_count);
        if (!s->tsl2591.saturated) {
            json_add(w, ",\"lux_centi\":%lu", (unsigned long)s->tsl2591.lux_centi);
        }
    }
    json_add(w, "},");

    json_add(w, "\"ltr390\":{\"ok\":%s", s->ltr390_ok ? "true" : "false");
    if (s->ltr390_ok) {
        json_add(w, ",\"uvs_counts\":%lu,\"uvi_centi\":%lu,"
            "\"als_counts\":%lu,\"lux_centi\":%lu",
            (unsigned long)s->ltr390.uvs_counts, (unsigned long)s->ltr390.uvi_centi,
            (unsigned long)s->ltr390.als_counts, (unsigned long)s->ltr390.lux_centi);
    }
    json_add(w, "},");

    json_add(w, "\"sgp40\":{\"ok\":%s", s->sgp40_ok ? "true" : "false");
    if (s->sgp40_ok) {
        json_add(w, ",\"sraw_voc\":%u,\"index_ready\":%s",
            (unsigned)s->sgp40.sraw_voc, s->sgp40.index_ready ? "true" : "false");
        if (s->sgp40.index_ready) {
            json_add(w, ",\"voc_index\":%ld", (long)s->sgp40.voc_index);
        }
    }
    json_add(w, "},");

    json_add(w, "\"icm20948\":{\"ok\":%s", s->icm20948_ok ? "true" : "false");
    if (s->icm20948_ok) {
        json_add(w, ",\"accel_mg\":[%ld,%ld,%ld],\"gyro_mdps\":[%ld,%ld,%ld]",
            (long)s->icm20948.accel_mg[0], (long)s->icm20948.accel_mg[1],
            (long)s->icm20948.accel_mg[2], (long)s->icm20948.gyro_mdps[0],
            (long)s->icm20948.gyro_mdps[1], (long)s->icm20948.gyro_mdps[2]);
    }
    json_add(w, "},");

    bool mag_ok = s->icm20948_ok && s->icm20948.mag_valid;
    json_add(w, "\"ak09916\":{\"ok\":%s", mag_ok ? "true" : "false");
    if (mag_ok) {
        json_add(w, ",\"mag_centi_ut\":[%ld,%ld,%ld],\"calibrated\":false",
            (long)s->icm20948.mag_centi_ut[0], (long)s->icm20948.mag_centi_ut[1],
            (long)s->icm20948.mag_centi_ut[2]);
    } else if (s->icm20948_ok && s->icm20948.mag_overflow) {
        json_add(w, ",\"reason\":\"overflow\"");
    }
    json_add(w, "}}\n");
}

/* lwIP HTTPD custom file hooks; GET-only, no post handlers, no I2C reads. */
int fs_open_custom(struct fs_file *file, const char *name) {
    if (file == NULL || name == NULL) return 0;

    if (url_matches(name, "/") || url_matches(name, "/index.html")) {
        serve_literal(file, dashboard_response, sizeof(dashboard_response) - 1u);
        return 1;
    }
    if (!url_matches(name, "/api/v1/sensors.json")) return 0;

    if (!latest_valid) {
        serve_literal(file, initializing_response, sizeof(initializing_response) - 1u);
        return 1;
    }

    char *data = (char *)mem_malloc(SENSOR_JSON_CAPACITY);
    if (data == NULL) {
        serve_literal(file, resource_response, sizeof(resource_response) - 1u);
        return 1;
    }
    json_writer_t w = { data, SENSOR_JSON_CAPACITY, 0u, true };
    render_json(&w, &latest_snapshot);
    if (!w.valid) {
        mem_free(data);
        serve_literal(file, resource_response, sizeof(resource_response) - 1u);
        return 1;
    }

    serve_literal(file, data, w.used);
    file->pextension = (fs_file_extension *)data;
    return 1;
}

void fs_close_custom(struct fs_file *file) {
    if (file != NULL && file->pextension != NULL) {
        mem_free(file->pextension);
        file->pextension = NULL;
    }
}
