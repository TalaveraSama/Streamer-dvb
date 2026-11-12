'use strict';
const { spawnSync } = require('child_process');
const path = require('path');
const test = spawnSync('python3', [path.join(__dirname, 'test_astra_import.py')], {encoding:'utf8'});
if (test.status !== 0) {
  process.stderr.write((test.stdout || '') + (test.stderr || ''));
  process.exit(test.status || 1);
}
console.log('PASS: Astra channel preview, security and private staging');
