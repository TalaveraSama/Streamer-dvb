'use strict';
const assert = require('assert').strict;
const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');
const root = path.resolve(__dirname, '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'tvs-offline-test-'));
try {
  const read = fs.readFileSync('/etc/os-release', 'utf8');
  const value = key => (read.match(new RegExp('^' + key + '=(.*)$', 'm')) || [,''])[1].replace(/^"|"$/g, '');
  fs.copyFileSync(path.join(root, 'scripts/install_offline.sh'), path.join(tmp, 'install_offline.sh'));
  fs.writeFileSync(path.join(tmp, 'PLATFORM'), `${value('ID')} ${value('VERSION_ID')} ${os.arch()==='x64'?'x86_64':os.arch()}\n`);
  for (const file of ['TVStreammerSAT5', 'tvstreammersat5-ca-newcamd.so', 'oscam-mini/oscam-mini']) {
    const dest = path.join(tmp, file); fs.mkdirSync(path.dirname(dest), {recursive:true}); fs.copyFileSync('/bin/true', dest);
  }
  for (const file of ['web/vendor/mpegts.min.js','web/vendor/hls.min.js','web/vendor/adminlte-3.2.0.min.css','web/vendor/LICENSE.adminlte','packaging/systemd/tvstreammersat5.service','packaging/oscam-mini/oscam-mini.service']) {
    const dest = path.join(tmp,file); fs.mkdirSync(path.dirname(dest),{recursive:true}); fs.writeFileSync(dest,'fixture');
  }
  const bin = path.join(tmp,'bin'); fs.mkdirSync(bin);
  fs.writeFileSync(path.join(bin,'gst-inspect-1.0'),'#!/bin/sh\nexit 0\n',{mode:0o755});
  fs.writeFileSync(path.join(bin,'systemctl'),'#!/bin/sh\nexit 0\n',{mode:0o755});
  const run = (...args)=>cp.spawnSync('bash',[path.join(tmp,'install_offline.sh'),...args],{encoding:'utf8',env:{...process.env,PATH:bin+':'+process.env.PATH}});
  assert.equal(run('--check').status,0);
  fs.writeFileSync(path.join(tmp,'PLATFORM'),'ubuntu 0.0 invalid\n');
  const wrongPlatform = run('--check'); assert.notEqual(wrongPlatform.status,0); assert.match(wrongPlatform.stderr,/Este ZIP es para/);
  fs.writeFileSync(path.join(tmp,'PLATFORM'),`${value('ID')} ${value('VERSION_ID')} ${os.arch()==='x64'?'x86_64':os.arch()}\n`);
  fs.unlinkSync(path.join(tmp,'TVStreammerSAT5'));
  const missing = run('--check'); assert.notEqual(missing.status,0); assert.match(missing.stderr,/Falta TVStreammerSAT5/);
  assert.notEqual(run('--unknown').status,0);
  console.log('PASS: offline bundle platform and preflight checks');
} finally { fs.rmSync(tmp,{recursive:true,force:true}); }
