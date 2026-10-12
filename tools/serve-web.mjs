import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
const root = new URL('../docs/', import.meta.url);
const files = new Map([['/', ['index.html', 'text/html']], ['/index.html', ['index.html', 'text/html']], ['/style.css', ['style.css', 'text/css']], ['/app.mjs', ['app.mjs', 'text/javascript']], ['/transport.mjs', ['transport.mjs', 'text/javascript']]]);
createServer(async (req, res) => {
  const file = files.get(new URL(req.url, 'http://localhost').pathname);
  if (!file) { res.writeHead(404); res.end('Not found'); return; }
  try { const body = await readFile(new URL(file[0], root)); res.writeHead(200, { 'Content-Type': file[1] + '; charset=utf-8', 'Cache-Control': 'no-store' }); res.end(body); }
  catch { res.writeHead(500); res.end('Could not read page'); }
}).listen(8080, '127.0.0.1', () => console.log('ESP32 test page: http://localhost:8080'));
