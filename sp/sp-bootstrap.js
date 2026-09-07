// embedder-agnostic shim layer for running the rsc-server bundle inside a host providing globalThis.__host

(function () {
    const H = globalThis.__host;
    function plog(s) { H.print(s); }
    function fmt(args) {
        return args
            .map((a) => {
                try {
                    if (typeof a === "string") return a;
                    // QuickJS's Error.stack is frames only, keep the message
                    if (a instanceof Error) return (a.name || "Error") + ": " + a.message + (a.stack ? "\n" + a.stack : "");
                    return JSON.stringify(a);
                } catch (e) {
                    return String(a);
                }
            })
            .join(" ");
    }

    // global aliases (UMD prelude checks `global` first, then self)
    globalThis.global = globalThis;
    globalThis.self = globalThis;
    globalThis.window = globalThis;

    // console (assert required by an inlined dep)
    globalThis.console = {
        log: (...a) => plog("[js] " + fmt(a)),
        info: (...a) => plog("[js] " + fmt(a)),
        warn: (...a) => plog("[js][warn] " + fmt(a)),
        error: (...a) => plog("[js][ERR] " + fmt(a)),
        debug: (...a) => plog("[js] " + fmt(a)),
        assert: (c, ...a) => { if (!c) plog("[js][assert] " + fmt(a)); },
        trace: () => {},
        dir: (...a) => plog("[js] " + fmt(a)),
        table: (...a) => plog("[js] " + fmt(a)),
        group: () => {},
        groupCollapsed: () => {},
        groupEnd: () => {},
        count: () => {},
        time: () => {},
        timeEnd: () => {},
    };

    // crypto.getRandomValues (mandatory; randombytes throws without it)
    if (!globalThis.crypto) globalThis.crypto = {};
    globalThis.crypto.getRandomValues = function (arr) {
        H.fillRandom(arr);
        return arr;
    };

    // timers (host-driven clock; the world tick re-arms via setTimeout)
    const timers = new Map(); // id -> {fn, args, due, interval(-1 = one-shot)}
    let timerSeq = 1;
    function addTimer(fn, ms, repeat, args) {
        const id = timerSeq++;
        timers.set(id, {
            fn,
            args,
            due: H.now() + (ms || 0),
            interval: repeat ? ms || 0 : -1,
        });
        return id;
    }
    globalThis.setTimeout = (fn, ms, ...a) => addTimer(fn, ms, false, a);
    globalThis.setInterval = (fn, ms, ...a) => addTimer(fn, ms, true, a);
    globalThis.clearTimeout = (id) => { timers.delete(id); };
    globalThis.clearInterval = (id) => { timers.delete(id); };
    globalThis.setImmediate = (fn, ...a) => addTimer(fn, 0, false, a);
    globalThis.clearImmediate = (id) => { timers.delete(id); };
    globalThis.queueMicrotask = (fn) => { Promise.resolve().then(fn); };

    // worker message bridge (bundle registers two listeners)
    const listeners = [];
    globalThis.addEventListener = (type, fn) => {
        if (type === "message") listeners.push(fn);
    };
    globalThis.removeEventListener = (type, fn) => {
        if (type !== "message") return;
        const i = listeners.indexOf(fn);
        if (i >= 0) listeners.splice(i, 1);
    };
    function toBundle(data) {
        const ev = { data };
        for (const fn of listeners.slice()) {
            try { fn(ev); } catch (e) { console.error("listener:", e); }
        }
    }

    const sp = {
        ready: false,
        readyCount: 0,
        progressPct: 0,
        progressText: 'Starting server',
        // report boot progress for the loading screen, monotonic, clamped 0-100
        setProgress(pct, text) {
            if (typeof pct !== 'number' || Number.isNaN(pct)) {
                return;
            }
            if (pct < sp.progressPct) {
                return;
            }
            sp.progressPct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
            if (typeof text === 'string') {
                sp.progressText = text;
            }
            // push progress straight to the host each milestone during world init
            try {
                if (H.progress) {
                    H.progress(sp.progressPct, sp.progressText);
                }
            } catch (e) {
                // display-only
            }
        },
        pump(now) {
            let fired = 0;
            const due = [];
            for (const [id, t] of timers) {
                if (t.due <= now) due.push([id, t]);
            }
            due.sort((a, b) => a[1].due - b[1].due);
            for (const [id, t] of due) {
                if (!timers.has(id)) continue;
                if (t.interval >= 0) t.due = now + t.interval;
                else timers.delete(id);
                try { t.fn.apply(null, t.args || []); }
                catch (e) { console.error("timer:", e); }
                fired++;
            }
            return fired;
        },
        start(config) { toBundle({ type: "start", config }); },
        startDefault() {
            sp.start({
                worldID: 1,
                version: 204,
                members: true,
                experienceRate: 1,
                fatigue: true,
                rememberCombatStyle: true,
                // OpenRSC solo quality-of-life flags, defaulted on for single-player parity
                wantCustomBanks: true,
                wantBankNotes: false,
                wantCertDeposit: true,
                wantCerterBankExchange: true,
                wantDecanting: true,
                wantDropX: true,
                wantKeyboardShortcuts: true,
                wantBankPresets: false,
                wantEquipmentTab: false,
                // per-world feature toggles mirroring the C boot config
                tutorialIsland: false,
                wantSkillcapePerks: true,
                wantCombatOdyssey: true,
                wantPoisonNpcs: false,
                wantLeftclickWebs: false,
                wantMissingGuildGreetings: true,
                fasterYohnus: false,
                usesClasses: true,
                spawnIronMan: true,
                // second feature-toggle wave, same convention as above, all default ON
                customFiremaking: true,
                wantBetterJewelryCrafting: true,
                wantCustomLeather: true,
                wantNewRareDropTables: true,
                npcKillMessages: true,
                wantEnchantedCrowns: true,
                wantBatchProgression: true,
            });
        },
        connect(id) { toBundle({ type: "connect", id }); },
        send(id, u8) { toBundle({ type: "data", id, data: u8 }); },
        disconnect(id) { toBundle({ type: "disconnect", id }); },
    };

    // postMessage = bundle -> client (the host)
    globalThis.postMessage = function (msg) {
        if (!msg) return;
        if (msg.type === "ready") {
            sp.readyCount++;
            if (!sp.ready) {
                sp.ready = true;
                sp.setProgress(100, "Ready");
                console.log("server ready");
            }
        } else if (msg.type === "data") {
            if (H.toClient) H.toClient(msg.id, msg.data);
        } else if (msg.type === "open") {
            console.log("server opened socket " + msg.id);
        } else if (msg.type === "close") {
            console.log("server closed socket " + msg.id);
        }
    };

    // persistent storage read by the idb-keyval shim; JSON round-trips numeric playerID and players string
    if (H.storageGet) {
        globalThis.__vitaStorage = {
            get(key) {
                const s = H.storageGet(key);
                return (s === null || s === undefined) ? undefined : JSON.parse(s);
            },
            set(key, value) { H.storageSet(key, JSON.stringify(value)); },
            del(key) { H.storageDel(key); },
            keys() { return []; },
        };
    }

    globalThis.__sp = sp;
})();
