'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

describe('firmware cotas_parse.h (host)', () => {
  it('parses the same QuotaSnapshot the station emits', () => {
    const src = path.join(__dirname, 'cotas_parse_host.cpp');
    const bin = path.join(os.tmpdir(), `cotas_parse_host_${process.pid}`);
    execFileSync('g++', ['-std=c++17', '-O0', '-o', bin, src], { stdio: 'pipe' });
    const out = execFileSync(bin, { encoding: 'utf8' });
    assert.match(out, /ok/);
    fs.rmSync(bin, { force: true });
  });
});
