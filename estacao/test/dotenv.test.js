'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { loadDotEnv, stationConfig, STATION_MDNS_INSTANCE } = require('../server/index');

describe('loadDotEnv / stationConfig', () => {
  it('applies PORT HOST POLL_MS from .env when not already set', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'estacao-env-'));
    const file = path.join(dir, '.env');
    fs.writeFileSync(file, [
      'PORT=9999',
      'HOST=127.0.0.1',
      'MDNS_NAME=estacao',
      'POLL_MS=60000',
      '# comment',
      'IGNORED',
    ].join('\n'));
    const env = {};
    loadDotEnv({ filePath: file, env });
    const cfg = stationConfig(env);
    assert.equal(cfg.port, 9999);
    assert.equal(cfg.host, '127.0.0.1');
    assert.equal(cfg.mdnsName, STATION_MDNS_INSTANCE);
    assert.equal(cfg.mdnsIgnored, undefined);
    assert.equal(cfg.pollMs, 60000);
    fs.rmSync(dir, { recursive: true });
  });

  it('ignores a custom MDNS_NAME so the stick still finds instance estacao', () => {
    const cfg = stationConfig({ MDNS_NAME: 'my-box', PORT: '8787' });
    assert.equal(cfg.mdnsName, 'estacao');
    assert.equal(cfg.mdnsIgnored, 'my-box');
    assert.equal(stationConfig({ MDNS_NAME: 'Estacao' }).mdnsName, 'estacao');
    assert.equal(stationConfig({ MDNS_NAME: 'Estacao' }).mdnsIgnored, undefined);
    assert.equal(STATION_MDNS_INSTANCE, 'estacao');
  });

  it('does not override existing process env', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'estacao-env-'));
    const file = path.join(dir, '.env');
    fs.writeFileSync(file, 'PORT=1111\nHOST=0.0.0.0\n');
    const env = { PORT: '2222' };
    loadDotEnv({ filePath: file, env });
    const cfg = stationConfig(env);
    assert.equal(cfg.port, 2222);
    assert.equal(cfg.host, '0.0.0.0');
    fs.rmSync(dir, { recursive: true });
  });

  it('defaults when .env is absent', () => {
    const cfg = stationConfig({});
    assert.equal(cfg.port, 8787);
    assert.equal(cfg.host, '0.0.0.0');
    assert.equal(cfg.mdnsName, 'estacao');
    assert.equal(cfg.pollMs, 90_000);
  });
});
