'use strict';
const {spawnSync} = require('child_process');
const path = require('path');
const result = spawnSync('python3', [path.join(__dirname, 'test_flussonic_import.py')], {encoding:'utf8'});
if (result.status !== 0) {
  process.stderr.write((result.stdout || '') + (result.stderr || ''));
  process.exit(result.status || 1);
}
console.log('PASS: Flussonic preview, UDP mapping, conflicts and safe staging');
