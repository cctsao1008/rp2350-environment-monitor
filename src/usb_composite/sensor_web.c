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
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>RP2350 Environment Monitor</title>"
    "<style>"
    ":root{color-scheme:dark;font:15px system-ui,sans-serif}"
    "body{margin:0;background:#0c1624;color:#e8f0f7}"
    "main{max-width:900px;margin:auto;padding:24px 16px}"
    "header{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}"
    "h1{font-size:1.55rem;margin:0}p{color:#9fb2c5}"
    "#state{padding:7px 12px;border:1px solid #415166;border-radius:20px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px;margin-top:22px}"
    "article{background:#17283b;border:1px solid #294058;border-radius:14px;padding:17px}"
    "h2{font-size:1.05rem;margin:0 0 12px}"
    ".value{font-size:1.35rem;font-weight:700;margin:8px 0;word-break:break-word}"
    ".secondary{font-size:.85rem;color:#9fb2c5;line-height:1.65}"
    ".meta{margin-top:20px;font-size:.85rem;color:#9fb2c5}"
    "a{color:#86c9f5}"
    "</style></head><body><main>"
    "<header><div><h1>RP2350 Environment Monitor</h1>"
    "<p>Live sensor snapshot over USB CDC-NCM</p></div>"
    "<span id=\"state\" role=\"status\">Connecting</span></header>"
    "<section class=\"grid\">"
    "<article><h2>BME280 &middot; Environment</h2><div class=\"value\" id=\"bme\">--</div>"
    "<div class=\"secondary\">Temperature / RH / pressure</div></article>"
    "<article><h2>TSL2591 &middot; Light</h2><div class=\"value\" id=\"tsl\">--</div>"
    "<div class=\"secondary\">Estimated lux; saturated measurements withheld</div></article>"
    "<article><h2>LTR390 &middot; UV / ALS</h2><div class=\"value\" id=\"ltr\">--</div>"
    "<div class=\"secondary\">Estimated UV index and ambient lux</div></article>"
    "<article><h2>SGP40 &middot; VOC</h2><div class=\"value\" id=\"sgp\">--</div>"
    "<div class=\"secondary\">VOC Index, not concentration or ppm</div></article>"
    "<article><h2>ICM20948 &middot; Motion</h2><div class=\"value\" id=\"imu\">--</div>"
    "<div class=\"secondary\">Accelerometer g / gyroscope deg/s (XYZ)</div></article>"
    "<article><h2>AK09916 &middot; Magnetic field</h2><div class=\"value\" id=\"mag\">--</div>"
    "<div class=\"secondary\">XYZ in microtesla; uncalibrated</div></article>"
    "</section><p class=\"meta\" id=\"meta\">Waiting for first sample.</p>"
    "<p class=\"meta\">Read-only, local USB network. "
    "<a href=\"/api/v1/sensors.json\">Sensor JSON API</a></p>"
    "</main><script>"
    "'use strict';"
    "const el=(id)=>document.getElementById(id);"
    "const fix=(n,d)=>Number(n).toFixed(d);"
    "const triple=(a,unit,scale,dp)=>a.map(v=>fix(v/scale,dp)).join(', ')+' '+unit;"
    "async function refresh(){"
    "try{const response=await fetch('/api/v1/sensors.json',{cache:'no-store'});"
    "if(!response.ok)throw Error('HTTP '+response.status);"
    "const s=await response.json();"
    "el('state').textContent='Online';"
    "const b=s.bme280;"
    "el('bme').textContent=b.ok?"
    "fix(b.temperature_centi_c/100,2)+' C / '+fix(b.humidity_centi_rh/100,2)"
    "+'% / '+fix(b.pressure_pa/100,2)+' hPa':'Unavailable';"
    "const t=s.tsl2591;"
    "el('tsl').textContent=t.ok?(t.saturated?'Saturated':fix(t.lux_centi/100,2)+' lx'):'Unavailable';"
    "const l=s.ltr390;"
    "el('ltr').textContent=l.ok?fix(l.uvi_centi/100,2)+' UVI / '"
    "+fix(l.lux_centi/100,2)+' lx':'Unavailable';"
    "const g=s.sgp40;"
    "el('sgp').textContent=g.ok?(g.index_ready?String(g.voc_index):'Warming up'):'Unavailable';"
    "const i=s.icm20948;"
    "el('imu').textContent=i.ok?"
    "triple(i.accel_mg,'g',1000,3)+' / '+triple(i.gyro_mdps,'deg/s',1000,3):'Unavailable';"
    "const m=s.ak09916;"
    "el('mag').textContent=m.ok?triple(m.mag_centi_ut,'uT',100,2):'Unavailable';"
    "el('meta').textContent='Sample '+s.sequence+' | Uptime '+fix(s.uptime_ms/1000,1)"
    "+' s | 1 Hz source sampling';"
    "}catch(e){el('state').textContent='Disconnected';"
    "el('meta').textContent='Data unavailable: '+e.message;}"
    "}"
    "refresh();setInterval(refresh,1000);"
    "</script></body></html>";

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
