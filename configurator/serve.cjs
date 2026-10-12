// Serves the configurator on localhost and opens it in the default browser.
// Web Serial needs Chrome or Edge.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { exec } = require('node:child_process');

const root = __dirname;
const port = Number(process.env.PORT) || 5180;
const contentTypes = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8'
};

http.createServer((request, response) => {
  const requestedPath = request.url === '/' ? '/index.html' : request.url.split('?')[0];
  const filePath = path.resolve(root, `.${decodeURIComponent(requestedPath)}`);
  if (!filePath.startsWith(root) || !contentTypes[path.extname(filePath)] || !fs.existsSync(filePath)) {
    response.writeHead(404);
    response.end('Not found');
    return;
  }
  response.writeHead(200, { 'Content-Type': contentTypes[path.extname(filePath)], 'Cache-Control': 'no-store' });
  fs.createReadStream(filePath).pipe(response);
}).listen(port, '127.0.0.1', () => {
  const url = `http://127.0.0.1:${port}/`;
  console.log(`Macropad configurator: ${url}  (Ctrl+C to stop)`);
  if (process.platform === 'win32') exec(`start "" "${url}"`);
});
