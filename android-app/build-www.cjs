// Assembles www/, the web files the Android app ships: the configurator page
// from ../configurator, plus Capacitor and its Bluetooth LE plugin, which the
// page uses in place of Web Bluetooth (see transports.js).
const fs = require('node:fs');
const path = require('node:path');

const here = __dirname;
const configurator = path.resolve(here, '..', 'configurator');
const www = path.join(here, 'www');
const vendor = [
  ['node_modules/@capacitor/core/dist/capacitor.js', 'vendor/capacitor.js'],
  ['node_modules/@capacitor-community/bluetooth-le/dist/plugin.js', 'vendor/bluetooth-le.js']
];

fs.rmSync(www, { recursive: true, force: true });
fs.mkdirSync(path.join(www, 'vendor'), { recursive: true });

// The desktop build's file list, minus the folders only Electron needs.
const pages = require(path.join(configurator, 'package.json')).build.files.filter(file => !file.includes('/'));
for (const file of pages) fs.copyFileSync(path.join(configurator, file), path.join(www, file));
for (const [from, to] of vendor) fs.copyFileSync(path.join(here, from), path.join(www, to));

const indexPath = path.join(www, 'index.html');
const marker = '<script src="transports.js"></script>';
const html = fs.readFileSync(indexPath, 'utf8');
if (!html.includes(marker)) throw new Error('index.html no longer loads transports.js; update android-app/build-www.cjs');
const tags = vendor.map(([, to]) => `<script src="${to}"></script>`).join('\n  ');
fs.writeFileSync(indexPath, html.replace(marker, `${tags}\n  ${marker}`));
console.log(`www: ${pages.length} page files and ${vendor.length} vendor scripts`);
