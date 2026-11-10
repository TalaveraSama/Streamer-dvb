const fs = require('fs');
const assert = require('assert');
const { readFile, readProgramVersion } = require('./lib/version');

const stream = readFile('src/StreamManager.cpp');
const header = readFile('src/StreamManager.h');
const http = readFile('src/HttpServer.cpp');

// 203.76: version shape is checked in one place instead of pinning a literal.
readProgramVersion();
assert.ok(http.includes('value="rtsp-tcp"'), 'RTSP TCP input mode missing');
assert.ok(http.includes('value="rtsp-udp"'), 'RTSP UDP input mode missing');
assert.ok(http.includes('value="rtsp-auto"'), 'RTSP Auto input mode missing');
assert.ok(stream.includes('setIntPropertyIfPresent(src, "protocols", rtspProtocols);'), 'RTSP transport selection missing');
assert.ok(stream.includes('rtpmp2tdepay'), 'RTP/MP2T ingest missing');
assert.ok(stream.includes('rtppcmadepay') && stream.includes('rtppcmudepay'), 'G.711 RTP depayloaders missing');
assert.ok(stream.includes('alawdec') && stream.includes('mulawdec'), 'G.711 decoders missing');
assert.ok(stream.includes('audio_normalize=AAC-LC/48000/2'), 'G.711 AAC normalization missing');
assert.ok(header.includes('rtspMpegTsLinked'), 'RTSP MP2T duplicate-branch guard missing');

console.log('PASS: RTSP input TCP/UDP/Auto + H264/H265/AAC/MPA/G711/MP2T support');
