'use strict';

// 203.76: single entry point for the source-level regression suite.
//   node tests/run-all.js            # all tests/*.js
//   node tests/run-all.js --quiet    # only the failures and the summary
// The C++ tests are built and executed by ctest (see CMakeLists.txt).

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const testsDir = __dirname;
const repoRoot = path.resolve(testsDir, '..');
const quiet = process.argv.includes('--quiet');

const files = fs
  .readdirSync(testsDir)
  .filter((name) => name.endsWith('.js'))
  .filter((name) => name !== 'run-all.js')
  .sort();

if (files.length === 0) {
  console.error('No tests found in ' + testsDir);
  process.exit(1);
}

let passed = 0;
const failures = [];

for (const file of files) {
  const result = spawnSync(process.execPath, [path.join(testsDir, file)], {
    cwd: repoRoot,
    encoding: 'utf8',
  });
  const ok = result.status === 0;
  if (ok) {
    passed += 1;
    if (!quiet) {
      const line = (result.stdout || '').trim().split('\n').filter(Boolean).pop() || 'PASS';
      console.log(`  ok    ${file}  ${line}`);
    }
  } else {
    failures.push({ file, output: `${result.stdout || ''}${result.stderr || ''}`.trim() });
    console.error(`  FAIL  ${file}`);
  }
}

if (failures.length > 0) {
  for (const failure of failures) {
    console.error(`\n----- ${failure.file} -----`);
    console.error(failure.output);
  }
  console.error(`\n${passed}/${files.length} passed, ${failures.length} failed`);
  process.exit(1);
}

console.log(`\n${passed}/${files.length} passed`);
