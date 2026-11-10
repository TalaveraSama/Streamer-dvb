'use strict';

// 203.76: the regression tests used to pin the literal program version, so every
// version bump broke three tests and they had to be edited by hand. The version
// is now read from its single source of truth (src/AppVersion.h) and only its
// shape is asserted here.

const fs = require('fs');
const path = require('path');
const assert = require('assert');

const repoRoot = path.resolve(__dirname, '..', '..');

function readFile(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');
}

function readProgramVersion() {
  const header = readFile('src/AppVersion.h');
  const match = header.match(/kProgramVersion\s*=\s*"([^"]+)"/);
  assert.ok(match, 'src/AppVersion.h must define kProgramVersion');
  const version = match[1];
  assert.match(version, /^\d{3}\.\d{2}$/,
    `program version "${version}" must look like 203.76`);
  return version;
}

module.exports = { repoRoot, readFile, readProgramVersion };
