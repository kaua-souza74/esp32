"""API HTTP local da EstufaSmart: ingestão validada, SQLite e painel."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import os
import sqlite3

HOST = os.getenv("ESTUFA_HOST", "0.0.0.0")
PORT = int(os.getenv("ESTUFA_PORT", "8000"))
DB_PATH = Path(os.getenv("ESTUFA_DB", "estufasmart.sqlite3"))

def conectar_db():
    db = sqlite3.connect(DB_PATH)
    db.execute("""CREATE TABLE IF NOT EXISTS leituras (
        id INTEGER PRIMARY KEY AUTOINCREMENT, recebido_em TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
        device_id TEXT NOT NULL, placa TEXT NOT NULL, seq INTEGER NOT NULL,
        temp REAL NOT NULL, umid REAL NOT NULL, janela TEXT NOT NULL,
        luz_pct INTEGER NOT NULL, estado TEXT NOT NULL,
        UNIQUE(device_id, seq))""")
    return db

def validar(d):
    campos = {"device_id", "placa", "seq", "temp", "umid", "janela", "luz_pct", "estado"}
    if not isinstance(d, dict) or not campos.issubset(d):
        raise ValueError("JSON incompleto")
    if not isinstance(d["device_id"], str) or not 1 <= len(d["device_id"]) <= 64:
        raise ValueError("device_id inválido")
    if not isinstance(d["placa"], str) or not 1 <= len(d["placa"]) <= 80:
        raise ValueError("placa inválida")
    for key, low, high in (("temp", -10, 60), ("umid", 0, 100), ("luz_pct", 0, 100), ("seq", 0, 2147483647)):
        value = d[key]
        if key in ("temp", "umid") and value is None and d["estado"] == "FALHA_SENSOR":
            continue
        if key in ("temp", "umid") and value is None and d["estado"] == "FALHA_SENSOR":
            continue
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high:
            raise ValueError(f"{key} fora do intervalo")
    if d["janela"] not in ("aberta", "fechada"):
        raise ValueError("janela inválida")
    if d["estado"] not in ("NORMAL", "ATENCAO", "CRITICO", "RECONHECIDO", "FALHA_SENSOR"):
        raise ValueError("estado inválido")
    return d

class Handler(BaseHTTPRequestHandler):
    def responder(self, status, payload, tipo="application/json; charset=utf-8"):
        corpo = payload if isinstance(payload, bytes) else json.dumps(payload, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(corpo)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(corpo)

    def do_POST(self):
        if self.path != "/api/telemetria":
            return self.responder(404, {"erro": "rota não encontrada"})
        try:
            tamanho = int(self.headers.get("Content-Length", "0"))
            if not 1 <= tamanho <= 4096:
                raise ValueError("tamanho de requisição inválido")
            leitura = validar(json.loads(self.rfile.read(tamanho)))
            with conectar_db() as db:
                db.execute("""INSERT INTO leituras(device_id,placa,seq,temp,umid,janela,luz_pct,estado)
                    VALUES(:device_id,:placa,:seq,:temp,:umid,:janela,:luz_pct,:estado)
                    ON CONFLICT(device_id,seq) DO NOTHING""", leitura)
            self.responder(201, {"ok": True})
        except (ValueError, json.JSONDecodeError, TypeError) as exc:
            self.responder(400, {"erro": str(exc)})

    def do_GET(self):
        if self.path == "/api/leituras":
            with conectar_db() as db:
                rows = db.execute("SELECT recebido_em,device_id,seq,temp,umid,janela,luz_pct,estado FROM leituras ORDER BY id DESC LIMIT 100").fetchall()
            nomes = ("recebido_em", "device_id", "seq", "temp", "umid", "janela", "luz_pct", "estado")
            return self.responder(200, [dict(zip(nomes, r)) for r in reversed(rows)])
        if self.path in ("/", "/index.html"):
            return self.responder(200, PAINEL.encode(), "text/html; charset=utf-8")
        self.responder(404, {"erro": "rota não encontrada"})

    def log_message(self, fmt, *args):
        print("[%s] %s" % (self.log_date_time_string(), fmt % args))

PAINEL = r'''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EstufaSmart | Monitoramento</title><style>
:root{color-scheme:dark;--bg:#0b1412;--card:#13211d;--muted:#9ab1a5;--green:#6ee7a1;--line:#294039}*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at 15% 0,#18362a,var(--bg) 55%);color:#edf8f1;font:16px system-ui,sans-serif}.wrap{max-width:1050px;margin:40px auto;padding:0 22px}header{display:flex;justify-content:space-between;align-items:center;gap:20px}h1{margin:0;font-size:clamp(2rem,5vw,3.4rem);letter-spacing:-.05em}header p,.muted{color:var(--muted)}.tag{color:var(--green);border:1px solid var(--line);border-radius:99px;padding:8px 13px}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin:30px 0}.card{background:linear-gradient(140deg,#172720,#111c19);border:1px solid var(--line);border-radius:20px;padding:22px;box-shadow:0 16px 40px #0003}.label{color:var(--muted);font-size:.85rem;text-transform:uppercase;letter-spacing:.12em}.value{font-size:2.4rem;font-weight:700;margin-top:10px}.unit{font-size:1rem;color:var(--muted)}.chart{height:300px;width:100%}canvas{width:100%;height:240px}.foot{margin-top:20px;color:var(--muted);font-size:.9rem}@media(max-width:700px){.grid{grid-template-columns:1fr}header{align-items:flex-start;flex-direction:column}}
</style><main class="wrap"><header><div><div class="tag">● ESTAÇÃO EM MONITORAMENTO</div><h1>Estufa<span style="color:var(--green)">Smart</span></h1><p>Temperatura, umidade e estado da estufa em tempo real.</p></div><div id="atualizado" class="muted">Aguardando leituras…</div></header>
<section class="grid"><article class="card"><div class="label">Temperatura</div><div class="value" id="temp">—<span class="unit"> °C</span></div></article><article class="card"><div class="label">Estado</div><div class="value" id="estado">—</div></article><article class="card"><div class="label">Umidade</div><div class="value" id="umid">—<span class="unit"> %</span></div></article></section>
<section class="card chart"><div class="label">Histórico de temperatura · últimas 100 leituras</div><canvas id="grafico" width="960" height="240"></canvas><div class="foot" id="detalhe">—</div></section></main><script>
async function atualizar(){try{const r=await fetch('/api/leituras');const dados=await r.json();if(!dados.length)return;const u=dados.at(-1);document.querySelector('#temp').innerHTML=`${u.temp==null?'—':u.temp.toFixed(1)}<span class="unit"> °C</span>`;document.querySelector('#umid').innerHTML=`${u.umid==null?'—':u.umid.toFixed(0)}<span class="unit"> %</span>`;document.querySelector('#estado').textContent=u.estado;document.querySelector('#atualizado').textContent=`${u.device_id} · seq ${u.seq}`;document.querySelector('#detalhe').textContent=`Janela ${u.janela} · luz ${u.luz_pct}% · recebido ${u.recebido_em}`;const c=document.querySelector('#grafico'),x=c.getContext('2d'),w=c.width,h=c.height,p=28;x.clearRect(0,0,w,h);x.strokeStyle='#294039';x.fillStyle='#9ab1a5';x.font='13px system-ui';for(let t=20;t<=30;t+=5){let y=h-p-(t-10)/30*(h-2*p);x.beginPath();x.moveTo(p,y);x.lineTo(w-p,y);x.stroke();x.fillText(t+'°',2,y+4)}const pontos=dados.filter(d=>d.temp!=null);if(pontos.length>1){x.beginPath();pontos.forEach((d,i)=>{let px=p+i*(w-2*p)/(pontos.length-1),py=h-p-(d.temp-10)/30*(h-2*p);i?x.lineTo(px,py):x.moveTo(px,py)});x.strokeStyle='#6ee7a1';x.lineWidth=3;x.stroke()}}catch(e){document.querySelector('#atualizado').textContent='Servidor indisponível'}}atualizar();setInterval(atualizar,5000);
</script></html>'''

if __name__ == "__main__":
    with conectar_db():
        pass
    print(f"EstufaSmart disponível em http://localhost:{PORT} (rede: http://192.168.37.2:{PORT})")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
