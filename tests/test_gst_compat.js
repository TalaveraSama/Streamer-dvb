'use strict';
// Compile and run the real compatibility header against both API surfaces.
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { spawnSync } = require('child_process');
const root = path.resolve(__dirname, '..');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'tvs-gst-compat-'));
try {
  fs.mkdirSync(path.join(temp, 'gst'));
  fs.writeFileSync(path.join(temp, 'gst/gst.h'), `
#pragma once
struct GstElement {};
struct GstPad { int api; };
#if TEST_MODERN
#define GST_CHECK_VERSION(a,b,c) 1
inline GstPad* gst_element_request_pad_simple(GstElement*, const char*) { static GstPad pad{20}; return &pad; }
#else
#define GST_CHECK_VERSION(a,b,c) 0
inline GstPad* gst_element_get_request_pad(GstElement*, const char*) { static GstPad pad{16}; return &pad; }
#endif
`);
  fs.writeFileSync(path.join(temp, 'test.cpp'), `
#include "GstCompat.h"
int main() { GstElement e; return tvsRequestPad(&e, "sink_%u")->api == EXPECTED ? 0 : 1; }
`);
  for (const [modern, expected] of [[0, 16], [1, 20]]) {
    const output = path.join(temp, `test-${modern}`);
    const build = spawnSync('g++', ['-std=c++17', '-Wall', '-Werror', `-DTEST_MODERN=${modern}`,
      `-DEXPECTED=${expected}`, `-I${temp}`, `-I${path.join(root, 'src')}`,
      path.join(temp, 'test.cpp'), '-o', output], { encoding:'utf8' });
    assert.strictEqual(build.status, 0, build.stderr || build.error?.message);
    const run = spawnSync(output, [], { encoding:'utf8' });
    assert.strictEqual(run.status, 0, run.stderr || run.error?.message);
  }
  for (const file of ['src/StreamManager.cpp', 'src/TranscoderModule.cpp']) {
    const source = fs.readFileSync(path.join(root, file), 'utf8');
    assert(source.includes('#include "GstCompat.h"'));
    assert(!source.includes('gst_element_request_pad_simple('));
    assert(source.includes('tvsRequestPad('));
  }
  console.log('PASS: GStreamer request pad compatibility for 1.16 and 1.20+');
} finally { fs.rmSync(temp, { recursive:true, force:true }); }
