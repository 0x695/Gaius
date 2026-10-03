// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius in the browser: the page around the engine (web/README.md).
//
// The engine is the desktop game compiled by Emscripten. This script
//   * mounts the browser's IndexedDB at /persist (settings, saves and the player's game files live there),
//   * imports the player's own copy of Caesar -- a dropped or chosen folder, or a zip of one -- into /persist/game,
//     validating that it is Caesar's (HOUSES.PL8 and EMPIRE2.001 beside each other),
//   * starts the game with the Play click (browsers only let a page make sound after a click),
//   * and offers the saves back as a download, and takes saves from the original or from other copies of Gaius.
// Nothing is uploaded anywhere and no game file is part of the build.
'use strict';

var Module = (function () {
  const $ = (id) => document.getElementById(id);
  const GAME = '/persist/game';
  const SAVES = '/persist/saves';
  const SAVE_SIZE = 57126;                                   // formats::save::kSaveSize
  const SKIP = new Set(['EXE', 'COM', 'DRV', 'BAT', 'DLL', 'SAV']);  // nothing Gaius reads
  const params = new URLSearchParams(location.search);

  let playing = false;
  let runtimeUp = false;

  // ---- storage ---------------------------------------------------------------------------------------------------

  function mountStorage() {
    const FS = Module.FS;
    FS.mkdir('/persist');
    FS.mount(Module.IDBFS, {}, '/persist');
    Module.addRunDependency('persist');
    FS.syncfs(true, function (err) {
      if (err) console.warn('storage: could not read the browser storage:', err);
      for (const d of [GAME, SAVES, '/persist/settings']) {
        try { FS.mkdir(d); } catch (e) { /* exists */ }
      }
      Module.removeRunDependency('persist');
    });
  }

  function persist() {
    return new Promise(function (resolve) {
      Module.FS.syncfs(false, function (err) {
        if (err) console.warn('storage: could not write the browser storage:', err);
        resolve();
      });
    });
  }

  let flushTimer = null;
  let flushing = false;
  // Called by the engine every few seconds and after a save (platform/web.cpp).
  function gaiusFlush() {
    if (!runtimeUp || flushTimer || flushing) return;
    flushTimer = setTimeout(async function () {
      flushTimer = null;
      flushing = true;
      await persist();
      flushing = false;
    }, 200);
  }

  function listDir(path) {
    try { return Module.FS.readdir(path).filter((n) => n !== '.' && n !== '..'); } catch (e) { return []; }
  }

  function hasGame() {
    const names = new Set(listDir(GAME).map((n) => n.toUpperCase()));
    return names.has('HOUSES.PL8') && names.has('EMPIRE2.001');
  }

  function gameSummary() {
    let bytes = 0;
    const names = listDir(GAME);
    for (const n of names) {
      try { bytes += Module.FS.stat(GAME + '/' + n).size; } catch (e) { /* ignore */ }
    }
    return names.length + ' files, ' + (bytes / 1048576).toFixed(1) + ' MB';
  }

  // ---- what the card says ---------------------------------------------------------------------------------------

  function card(title, text) {
    $('card-title').textContent = title;
    $('card-text').textContent = text;
  }
  function problem(text) {
    const p = $('problem');
    p.hidden = !text;
    p.textContent = text || '';
  }
  function progress(value) {
    const p = $('progress');
    if (value === null) { p.hidden = true; return; }
    p.hidden = false;
    p.value = value;
  }

  function showState() {
    document.body.classList.remove('loading');
    $('card').hidden = false;
    progress(null);
    for (const b of ['btn-saves', 'btn-data']) $(b).hidden = false;
    if (hasGame()) {
      card('Ready', 'Your copy of Caesar is in this browser.');
      $('drop').hidden = true;
      $('ready').hidden = false;
      $('ready-note').textContent = 'Game data: ' + gameSummary() + '.';
    } else {
      card('Play Caesar in your browser', 'Gaius is an open-source engine for Caesar (1992). It needs the files of your own copy of the game.');
      $('drop').hidden = false;
      $('ready').hidden = true;
    }
  }

  // ---- importing the player's files ------------------------------------------------------------------------------

  // An entry is {path, read(): Promise<Uint8Array>}; the importers below make them from a folder, files or a zip.

  function fileEntry(path, file) {
    return { path: path, read: async () => new Uint8Array(await file.arrayBuffer()) };
  }

  async function inflateRaw(raw) {
    if (typeof DecompressionStream === 'undefined')
      throw new Error('This browser cannot read compressed zips. Choose the folder instead, or use a recent Chrome, Edge, Firefox or Safari.');
    const stream = new Blob([raw]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  async function zipEntries(file) {
    const buf = new Uint8Array(await file.arrayBuffer());
    const dv = new DataView(buf.buffer);
    let eocd = -1;
    for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--) {
      if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) throw new Error(file.name + ' is not a zip file.');
    const count = dv.getUint16(eocd + 10, true);
    let p = dv.getUint32(eocd + 16, true);
    const out = [];
    for (let n = 0; n < count; n++) {
      if (dv.getUint32(p, true) !== 0x02014b50) throw new Error(file.name + ' is damaged.');
      const method = dv.getUint16(p + 10, true);
      const csize = dv.getUint32(p + 20, true);
      const nlen = dv.getUint16(p + 28, true), elen = dv.getUint16(p + 30, true), clen = dv.getUint16(p + 32, true);
      const lho = dv.getUint32(p + 42, true);
      const name = new TextDecoder().decode(buf.subarray(p + 46, p + 46 + nlen));
      p += 46 + nlen + elen + clen;
      if (name.endsWith('/')) continue;
      out.push({
        path: name,
        read: async function () {
          const start = lho + 30 + dv.getUint16(lho + 26, true) + dv.getUint16(lho + 28, true);
          const raw = buf.subarray(start, start + csize);
          if (method === 0) return raw.slice();
          if (method === 8) return inflateRaw(raw);
          throw new Error('A file in ' + file.name + ' uses a compression this page cannot read.');
        },
      });
    }
    return out;
  }

  // Files from <input>: a folder (webkitRelativePath) or loose files, a zip among them expanded.
  async function entriesFromFiles(files) {
    const out = [];
    for (const f of files) {
      if (/\.zip$/i.test(f.name)) out.push(...(await zipEntries(f)));
      else out.push(fileEntry(f.webkitRelativePath || f.name, f));
    }
    return out;
  }

  function readAllEntries(reader) {
    return new Promise(function (resolve, reject) {
      const all = [];
      (function next() {
        reader.readEntries(function (batch) {
          if (!batch.length) resolve(all);
          else { all.push(...batch); next(); }
        }, reject);
      })();
    });
  }

  async function walk(entry, prefix, out) {
    if (entry.isFile) {
      const file = await new Promise((res, rej) => entry.file(res, rej));
      if (/\.zip$/i.test(file.name) && !prefix) out.push(...(await zipEntries(file)));
      else out.push(fileEntry(prefix + entry.name, file));
    } else if (entry.isDirectory) {
      for (const child of await readAllEntries(entry.createReader())) await walk(child, prefix + entry.name + '/', out);
    }
  }

  // The folder that holds the game: HOUSES.PL8 and EMPIRE2.001 side by side; the US build's folder first.
  function findGameFolder(entries) {
    const dirs = new Map();
    for (const e of entries) {
      const parts = e.path.replace(/\\/g, '/').split('/').filter(Boolean);
      const name = parts.pop();
      const dir = parts.join('/');
      if (!dirs.has(dir)) dirs.set(dir, new Map());
      dirs.get(dir).set(name.toUpperCase(), { name: name, entry: e });
    }
    const found = [...dirs.entries()].filter(([, m]) => m.has('HOUSES.PL8') && m.has('EMPIRE2.001'));
    if (!found.length) return null;
    const score = ([dir]) => (/(^|\/)us$/i.test(dir) ? 0 : 1) * 100 + dir.split('/').length;
    found.sort((a, b) => score(a) - score(b));
    return found[0][1];
  }

  async function importEntries(entries) {
    problem('');
    const folder = findGameFolder(entries);
    if (!folder) {
      throw new Error("That doesn't look like Caesar: no folder with both HOUSES.PL8 and EMPIRE2.001 in it. " +
        'Choose the folder that holds CSR.EXE (the US release; GOG keeps it in a folder called US).');
    }
    const FS = Module.FS;
    for (const n of listDir(GAME)) { try { FS.unlink(GAME + '/' + n); } catch (e) { /* ignore */ } }
    const files = [...folder.values()].filter((f) => !SKIP.has((f.name.split('.').pop() || '').toUpperCase()));
    let done = 0;
    card('Importing', 'Copying your game files into this browser.');
    $('drop').hidden = true;
    for (const f of files) {
      FS.writeFile(GAME + '/' + f.name, await f.entry.read());
      progress((100 * ++done) / files.length);
      if (done % 8 === 0) await new Promise((r) => setTimeout(r, 0));
    }
    await persist();
  }

  async function runImport(getEntries) {
    try {
      progress(0);
      await importEntries(await getEntries());
      showState();
      return true;
    } catch (e) {
      showState();
      problem(e && e.message ? e.message : String(e));
      return false;
    }
  }

  // ---- saves ---------------------------------------------------------------------------------------------------

  let crcTable = null;
  function crc32(bytes) {
    if (!crcTable) {
      crcTable = new Uint32Array(256);
      for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        crcTable[n] = c >>> 0;
      }
    }
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = crcTable[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  function makeZip(files) {  // store-only
    const now = new Date();
    const time = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
    const date = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
    const parts = [];
    const central = [];
    let offset = 0;
    const enc = new TextEncoder();
    for (const f of files) {
      const name = enc.encode(f.name);
      const crc = crc32(f.data);
      const local = new DataView(new ArrayBuffer(30));
      local.setUint32(0, 0x04034b50, true); local.setUint16(4, 20, true);
      local.setUint16(10, time, true); local.setUint16(12, date, true);
      local.setUint32(14, crc, true); local.setUint32(18, f.data.length, true); local.setUint32(22, f.data.length, true);
      local.setUint16(26, name.length, true);
      parts.push(new Uint8Array(local.buffer), name, f.data);
      const c = new DataView(new ArrayBuffer(46));
      c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true);
      c.setUint16(12, time, true); c.setUint16(14, date, true);
      c.setUint32(16, crc, true); c.setUint32(20, f.data.length, true); c.setUint32(24, f.data.length, true);
      c.setUint16(28, name.length, true); c.setUint32(42, offset, true);
      central.push(new Uint8Array(c.buffer), name);
      offset += 30 + name.length + f.data.length;
    }
    let size = 0;
    for (const p of central) size += p.length;
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, files.length, true); end.setUint16(10, files.length, true);
    end.setUint32(12, size, true); end.setUint32(16, offset, true);
    return new Blob([...parts, ...central, new Uint8Array(end.buffer)], { type: 'application/zip' });
  }

  function download(blob, name) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  }

  function downloadSaves() {
    const files = listDir(SAVES).filter((n) => /\.sav$/i.test(n))
      .map((n) => ({ name: n, data: Module.FS.readFile(SAVES + '/' + n) }));
    if (!files.length) { alert('There are no saves yet. Save a game from the Forum first.'); return; }
    download(makeZip(files), 'gaius-saves.zip');
  }

  async function addSaves(fileList) {
    const FS = Module.FS;
    const taken = new Set(listDir(SAVES).map((n) => n.toUpperCase()));
    const added = [], skipped = [];
    for (const f of fileList) {
      const data = new Uint8Array(await f.arrayBuffer());
      if (data.length !== SAVE_SIZE) { skipped.push(f.name); continue; }
      let name = f.name.toUpperCase();
      if (!/^CAESAR0[1-8]\.SAV$/.test(name)) {
        name = null;
        for (let i = 1; i <= 8 && !name; i++) {
          const slot = 'CAESAR0' + i + '.SAV';
          if (!taken.has(slot)) name = slot;
        }
      }
      if (!name) { skipped.push(f.name + ' (all eight slots are full)'); continue; }
      FS.writeFile(SAVES + '/' + name, data);
      taken.add(name);
      added.push(f.name + ' as ' + name);
    }
    await persist();
    alert((added.length ? 'Added: ' + added.join(', ') + '.\nLoad them from the Forum.' : 'Nothing added.') +
      (skipped.length ? '\nSkipped (not a Caesar save): ' + skipped.join(', ') + '.' : ''));
  }

  // ---- the small menus in the bar ---------------------------------------------------------------------------------

  function closeMenu() { $('menu').hidden = true; }
  function openMenu(anchor, items, note) {
    const m = $('menu');
    m.replaceChildren();
    for (const it of items) {
      const b = document.createElement('button');
      b.type = 'button';
      b.textContent = it.label;
      b.addEventListener('click', function () { closeMenu(); it.run(); });
      m.appendChild(b);
    }
    if (note) {
      const p = document.createElement('p');
      p.className = 'note';
      p.textContent = note;
      m.appendChild(p);
    }
    m.hidden = false;
  }

  function wireUi() {
    $('btn-saves').addEventListener('click', function (ev) {
      ev.stopPropagation();
      if (!$('menu').hidden) { closeMenu(); return; }
      openMenu(ev.currentTarget, [
        { label: 'Download my saves (.zip)', run: downloadSaves },
        { label: 'Add saves from files…', run: () => $('in-saves').click() },
      ], 'Saves are original-format .SAV files. Gaius loads slots CAESAR01 to CAESAR08.');
    });
    $('btn-data').addEventListener('click', function (ev) {
      ev.stopPropagation();
      if (!$('menu').hidden) { closeMenu(); return; }
      const items = [];
      if (!playing) {
        items.push({ label: 'Replace game data…', run: () => { problem(''); card('Replace game data', 'Give Gaius a different copy of Caesar.'); $('ready').hidden = true; $('drop').hidden = false; } });
        items.push({
          label: 'Remove game data from this browser',
          run: async function () {
            if (!confirm('Remove your Caesar files from this browser? Your saves stay.')) return;
            for (const n of listDir(GAME)) { try { Module.FS.unlink(GAME + '/' + n); } catch (e) { /* ignore */ } }
            await persist();
            showState();
          },
        });
      }
      openMenu(ev.currentTarget, items, playing ? 'Reload the page to change the game data.' : '');
    });
    $('btn-fullscreen').addEventListener('click', function () {
      const el = $('screen');
      if (document.fullscreenElement) document.exitFullscreen();
      else if (el.requestFullscreen) el.requestFullscreen();
    });
    // The right button is the game's (it switches between placing and the toolbar): no browser menu over the canvas.
    $('canvas').addEventListener('contextmenu', (ev) => ev.preventDefault());
    document.addEventListener('click', closeMenu);
    document.addEventListener('keydown', function (ev) { if (ev.key === 'Escape') closeMenu(); });
    $('in-saves').addEventListener('change', function (ev) { addSaves([...ev.target.files]); ev.target.value = ''; });

    $('pick-folder').addEventListener('click', () => $('in-folder').click());
    $('pick-files').addEventListener('click', () => $('in-files').click());
    for (const id of ['in-folder', 'in-files']) {
      $(id).addEventListener('change', function (ev) {
        const files = [...ev.target.files];
        ev.target.value = '';
        if (files.length) runImport(() => entriesFromFiles(files));
      });
    }
    const drop = $('drop');
    document.addEventListener('dragover', function (ev) { if (!playing) { ev.preventDefault(); if (!drop.hidden) drop.classList.add('over'); } });
    document.addEventListener('dragleave', () => drop.classList.remove('over'));
    document.addEventListener('drop', function (ev) {
      ev.preventDefault();
      drop.classList.remove('over');
      if (playing || drop.hidden) return;
      const dt = ev.dataTransfer;
      const roots = [];  // taken now: the event's items are gone once we await
      for (const item of dt.items || []) { const e = item.webkitGetAsEntry && item.webkitGetAsEntry(); if (e) roots.push(e); }
      runImport(async function () {
        const out = [];
        for (const e of roots) await walk(e, '', out);
        return out;
      });
    });
    $('play').addEventListener('click', play);

    // A game in progress is not saved until the player saves it: ask before the page goes.
    window.addEventListener('beforeunload', function (ev) {
      if (playing) { ev.preventDefault(); ev.returnValue = ''; }
    });
    window.addEventListener('pagehide', persist);
    document.addEventListener('visibilitychange', function () { if (document.visibilityState === 'hidden' && runtimeUp) persist(); });
  }

  // ---- playing -------------------------------------------------------------------------------------------------

  function play() {
    if (!hasGame()) { showState(); return; }
    playing = true;
    closeMenu();
    document.body.classList.add('playing');
    $('btn-fullscreen').hidden = false;
    $('screen').hidden = false;
    $('canvas').focus();
    const extra = (params.get('args') || '').split(/\s+/).filter(Boolean);  // e.g. ?args=--mute%20--no-intro
    try {
      Module.callMain([GAME].concat(extra));
    } catch (e) {
      fail(e);
    }
  }

  function ended() {
    playing = false;
    persist();
    document.body.classList.remove('playing');
    $('screen').hidden = true;
    $('btn-fullscreen').hidden = true;
    $('card').hidden = false;
    $('drop').hidden = true;
    $('ready').hidden = false;
    $('play').textContent = 'Play again';
    $('play').onclick = () => location.reload();
    card('Gaius has closed', 'Your saves are kept in this browser.');
  }

  function fail(e) {
    console.error(e);
    playing = false;
    document.body.classList.remove('playing');
    $('screen').hidden = true;
    $('card').hidden = false;
    $('drop').hidden = true;
    $('ready').hidden = false;
    $('play').textContent = 'Reload';
    $('play').onclick = () => location.reload();
    card('Something went wrong', 'The game stopped. Your saves are kept; reload to try again.');
    problem(e && e.message ? e.message : String(e));
  }

  // ---- start-up ------------------------------------------------------------------------------------------------

  async function fromUrl(url) {  // ?data=<url of a zip>: for hosting and for tests; the zip is read like a dropped one
    try {
      const response = await fetch(url);
      if (!response.ok) throw new Error('Could not fetch ' + url + ' (' + response.status + ').');
      const blob = await response.blob();
      return zipEntries(new File([blob], 'game.zip'));
    } catch (e) {
      throw e;
    }
  }

  async function ready() {
    runtimeUp = true;
    wireUi();
    const url = params.get('data');
    if (url) {
      const ok = await runImport(() => fromUrl(url));
      if (ok && params.get('autoplay') === '1') play();
      return;
    }
    showState();
  }

  return {
    canvas: $('canvas'),
    noInitialRun: true,
    preRun: [mountStorage],
    print: (t) => console.log(t),
    printErr: (t) => console.warn(t),
    setStatus: function (text) {
      const m = /\((\d+)\/(\d+)\)/.exec(text || '');
      if (m) { card('Starting the engine', 'Downloading the game engine.'); progress((100 * m[1]) / m[2]); }
    },
    onRuntimeInitialized: ready,
    onExit: ended,
    onAbort: (what) => fail(new Error('The engine stopped: ' + what)),
    gaiusFlush: gaiusFlush,
  };
})();

window.addEventListener('error', function (ev) { console.error(ev.error || ev.message); });
