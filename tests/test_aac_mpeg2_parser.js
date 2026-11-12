'use strict';
// Exercise the actual parser-selection function with lightweight GStreamer stubs.
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const {spawnSync} = require('child_process');
const source = fs.readFileSync(path.join(__dirname, '../src/StreamManager.cpp'), 'utf8');
const start = source.indexOf('std::string parserForCaps(');
const end = source.indexOf('\nGstElement* capsFilterForMux(', start);
assert(start >= 0 && end > start, 'parser selector not found');
const selector = source.slice(start, end);
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'tvs-aac-parser-'));
try {
  const test = `
#include <string>
#include <cstring>
#include <iostream>
using gint = int;
struct GstStructure { const char* name; int version; int layer; };
struct GstCaps { GstStructure st; };
int g_strcmp0(const char* a, const char* b) { if (!a || !b) return a == b ? 0 : 1; return std::strcmp(a,b); }
int gst_caps_get_size(GstCaps*) { return 1; }
const GstStructure* gst_caps_get_structure(GstCaps* c, int) { return &c->st; }
const char* gst_structure_get_name(const GstStructure* s) { return s->name; }
bool gst_structure_get_int(const GstStructure* s, const char* name, gint* out) {
  *out = std::strcmp(name,"mpegversion") == 0 ? s->version : s->layer; return true;
}
${selector}
int main() {
  GstCaps aac2{{"audio/mpeg", 2, 0}}, aac4{{"audio/mpeg", 4, 0}};
  GstCaps mp3{{"audio/mpeg", 1, 3}}, unknown{{"audio/mpeg", 0, 0}};
  if (parserForCaps(&aac2,"audio/mpeg, mpegversion=(int)2") != "aacparse") return 1;
  if (parserForCaps(&aac4,"audio/mpeg, mpegversion=(int)4") != "aacparse") return 2;
  if (parserForCaps(&mp3,"audio/mpeg, mpegversion=(int)1") != "mpegaudioparse") return 3;
  if (parserForCaps(&unknown,"audio/mpeg") != "") return 4;
  if (parserForCaps(nullptr,"audio/mpeg, mpegversion=(int)2") != "aacparse") return 5;
  if (parserForCaps(nullptr,"audio/mpeg, mpegversion=2") != "aacparse") return 6;
  if (parserForCaps(nullptr,"video/x-h264") != "h264parse") return 7;
  if (parserForCaps(nullptr,"audio/x-ac3") != "ac3parse") return 8;
}
`;
  const cpp = path.join(temp, 'test.cpp'), exe = path.join(temp, 'test');
  fs.writeFileSync(cpp, test);
  const build = spawnSync('g++', ['-std=c++17', '-Wall', '-Werror', cpp, '-o', exe], {encoding:'utf8'});
  assert.strictEqual(build.status, 0, build.stderr || build.error?.message);
  const run = spawnSync(exe, [], {encoding:'utf8'});
  assert.strictEqual(run.status, 0, `parser selection failed: ${run.status} ${run.stderr}`);
  console.log('PASS: AAC MPEG-2/MPEG-4 parser selection and existing formats');
} finally { fs.rmSync(temp, {recursive:true, force:true}); }
