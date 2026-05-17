var RELAY_SERVERS = {
    na: 'na-relay.v0e.dev:25200',
    eu: 'eu-relay.v0e.dev:25200'
};

function applyRelayServersConfig(servers) {
    if (!servers) return;
    if (servers.na) RELAY_SERVERS.na = servers.na;
    if (servers.eu) RELAY_SERVERS.eu = servers.eu;
}

var _hostRelayRegion = 'off';
var _joinRelayRegion = 'na';

function relayId(prefix, suffix) {
    return prefix + suffix.charAt(0).toUpperCase() + suffix.slice(1);
}

function normalizeConnectionMode(mode) {
    return mode === 'Relay' ? 'Relay' : 'Direct';
}

function setRelayMode(prefix, mode) {
    document.getElementById(relayId(prefix, 'relayMode')).value = normalizeConnectionMode(mode);
    syncRelayUi(prefix);
}

function setRelayPreset(prefix, preset) {
    document.getElementById(relayId(prefix, 'relayPreset')).value = preset;
    syncRelayUi(prefix);
}

function syncRelayButtonGroup(selector, activeValue) {
    document.querySelectorAll(selector + ' .relay-pill').forEach(button => {
        button.classList.toggle('active', button.dataset.value === activeValue);
    });
}

function syncRelayUi(prefix) {
    const mode = normalizeConnectionMode(document.getElementById(relayId(prefix, 'relayMode')).value || 'Direct');
    document.getElementById(relayId(prefix, 'relayMode')).value = mode;
    const config = document.getElementById(relayId(prefix, 'relayConfig'));

    syncRelayButtonGroup('[data-relay-mode-group="' + prefix + '"]', mode);

    if (config) config.style.display = mode === 'Relay' ? '' : 'none';

    if (prefix === 'join') {
        const serverIP = document.getElementById('serverIP');
        const serverIPHint = document.getElementById('serverIPHint');
        const serverAddressGroup = document.getElementById('joinServerAddressGroup');
        if (mode === 'Relay') {
            serverAddressGroup.style.display = 'none';
            serverIP.placeholder = '';
            serverIPHint.textContent = 'The launcher will use the relay host automatically.';
        } else {
            serverAddressGroup.style.display = '';
            document.getElementById('joinServerAddressLabel').textContent = 'Server Address';
            serverIP.placeholder = 'LAN, public, or VPN address';
            serverIPHint.textContent = 'Use the host\'s LAN IP, public IP, or VPN address.';
        }
    }

    if (prefix === 'host') {
        const deviceIP = document.getElementById('deviceIP');
        const deviceIPLabel = document.getElementById('deviceIPLabel');
        const deviceIPHint = document.getElementById('deviceIPHint');
        const relayHint = document.getElementById('hostRelayHint');
        if (mode === 'Relay') {
            deviceIPLabel.textContent = 'Bind Address';
            deviceIP.placeholder = 'Auto-detected local IPv4';
            deviceIPHint.textContent = 'Usually correct as-is. The relay handles public reachability.';
            if (relayHint) relayHint.textContent = 'Your relay hostname, for example relay-udp.yourdomain.com:25200.';
        } else {
            deviceIPLabel.textContent = 'Bind Address';
            deviceIP.placeholder = 'Auto-detected local IPv4';
            deviceIPHint.textContent = 'Auto-detected for you. Change it only if you want to bind to a different adapter, including a VPN.';
        }

        updateDetectedDeviceIpNote();
    }
}

function updateDetectedDeviceIpNote() {
    const note = document.getElementById('deviceIPDetectedNote');
    if (!note) return;
    if (!detectedDeviceIP) {
        note.style.display = 'none';
        note.textContent = '';
        return;
    }
    note.style.display = '';
    note.textContent = 'Detected on this PC: ' + detectedDeviceIP;
}

// host relay region selector

function onRelayRegionChanged(region) {
    _hostRelayRegion = region;

    var ctrl = document.getElementById('hostRelayRegionControl');
    if (ctrl) {
        ctrl.querySelectorAll('.segmented-btn').forEach(function(btn) {
            btn.classList.toggle('active', btn.dataset.region === region);
        });
    }

    if (region === 'off') {
        document.getElementById('hostRelayMode').value = 'Direct';
        document.getElementById('hostRelayAddress').value = '';
        document.getElementById('hostRelayKey').value = '';
        document.getElementById('hostRelayCode').value = '';
        document.getElementById('hostRelayJoinLink').value = '';
        var codeVal = document.getElementById('hostRelayCodeValue');
        if (codeVal) codeVal.value = '';
        var codeDisplay = document.getElementById('hostRelayCodeDisplay');
        if (codeDisplay) codeDisplay.style.display = 'none';
    } else {
        document.getElementById('hostRelayMode').value = 'Relay';
        document.getElementById('hostRelayAddress').value = RELAY_SERVERS[region] || '';
    }
}

// join relay region selector

function onJoinRelayRegionChanged(region) {
    _joinRelayRegion = region;

    var ctrl = document.getElementById('joinRelayRegionControl');
    if (ctrl) {
        ctrl.querySelectorAll('.segmented-btn').forEach(function(btn) {
            btn.classList.toggle('active', btn.dataset.region === region);
        });
    }

    // only update hidden address if not overridden manually
    var manual = (document.getElementById('joinRelayAddressManual') || {}).value || '';
    if (!manual) {
        document.getElementById('joinRelayAddress').value = RELAY_SERVERS[region] || '';
    }
}

function getJoinRelayAddress() {
    var manual = (document.getElementById('joinRelayAddressManual') || {}).value || '';
    if (manual) return manual;
    var hidden = (document.getElementById('joinRelayAddress') || {}).value || '';
    if (hidden) return hidden;
    return RELAY_SERVERS[_joinRelayRegion] || RELAY_SERVERS.na;
}

function onRelayCodeInput(el) {
    var v = el.value;
    if (!v.startsWith('cypress://') && !v.startsWith('http://') && !v.startsWith('https://')) {
        el.value = v.toUpperCase();
    }
}

function parseRelayLink(prefix) {
    const codeInput = document.getElementById('joinRelayCode');
    if (!codeInput) return;
    const raw = codeInput.value.trim();
    if (!raw.startsWith('cypress://') && !raw.startsWith('http://') && !raw.startsWith('https://')) return;
    try {
        const link = new URL(raw);
        const addr = link.searchParams.get('relay') || '';
        const key = link.searchParams.get('key') || '';
        if (addr) document.getElementById('joinRelayAddress').value = addr;
        if (key) document.getElementById('joinRelayKey').value = key;
        codeInput.value = '';
        showStatus('Parsed legacy join link.', 'info');
    } catch (e) {
        showStatus('Could not parse relay link.', 'error');
    }
}

function resolveRelayCode() {
    const codeInput = document.getElementById('joinRelayCode');
    const code = (codeInput ? codeInput.value : '').trim().toUpperCase();
    if (!code) { showStatus('Enter a relay code first.', 'error'); return; }
    if (code.startsWith('CYPRESS://') || code.startsWith('HTTP')) {
        codeInput.value = code;
        parseRelayLink('join');
        return;
    }
    var relayAddr = getJoinRelayAddress();
    send('resolveRelayCode', { relayAddress: relayAddr, code: code });
}

var _autoResolveTimer = null;
function autoResolveRelayCode() {
    if (_autoResolveTimer) clearTimeout(_autoResolveTimer);
    var code = (document.getElementById('joinRelayCode').value || '').trim();
    if (/^[A-Z0-9]{6}$/.test(code)) {
        _autoResolveTimer = setTimeout(function() { resolveRelayCode(); }, 300);
    }
}

function onRelayResolved(data) {
    const infoEl = document.getElementById('joinRelayResolved');
    const hintEl = document.getElementById('joinRelayCodeHint');
    if (data.error) {
        if (infoEl) { infoEl.style.display = 'none'; infoEl.innerHTML = ''; }
        if (hintEl) hintEl.textContent = data.error;
        showStatus(data.error, 'error');
        return;
    }
    document.getElementById('joinRelayAddress').value = data.relayAddress || '';
    document.getElementById('joinRelayKey').value = data.relayKey || '';
    if (hintEl) hintEl.textContent = 'Code verified!';
    if (infoEl) {
        infoEl.style.display = '';
        infoEl.innerHTML = '<strong>' + escapeHtml(data.serverName || 'Server') + '</strong> <span class="text-muted">(' + escapeHtml(data.game || '?') + ')</span>';
    }
}

function copyRelayCode() {
    const code = document.getElementById('hostRelayCodeValue');
    if (!code) return;
    navigator.clipboard.writeText(code.value).then(function() {
        showStatus('Join link copied!', 'success');
    });
}

function requestRelayLeaseAndStart() {
    _pendingRelayStart = true;
    var motd = '';
    if (typeof getMotdRaw === 'function') motd = getMotdRaw();
    var serverName = motd || (getGame() + ' Server');
    document.getElementById('hostRelayServerName').value = serverName;
    var addr = document.getElementById('hostRelayAddress').value;
    if (!addr) {
        var fallbackRegion = _hostRelayRegion !== 'off' ? _hostRelayRegion : 'na';
        addr = RELAY_SERVERS[fallbackRegion];
        document.getElementById('hostRelayAddress').value = addr;
    }
    send('getRelayLease', {
        relayAddress: addr,
        relayServerName: serverName,
        game: getGame()
    });
    showStatus('Getting relay lease...', 'info');
}

function requestRelayLease() {
    var addr = document.getElementById('hostRelayAddress').value;
    if (!addr) {
        var fallbackRegion = _hostRelayRegion !== 'off' ? _hostRelayRegion : 'na';
        addr = RELAY_SERVERS[fallbackRegion];
        document.getElementById('hostRelayAddress').value = addr;
    }
    send('getRelayLease', {
        relayAddress: addr,
        relayServerName: document.getElementById('hostRelayServerName').value,
        game: getGame()
    });
}

var _pendingRelayStart = false;

function applyRelayLease(data) {
    if (data.relayAddress !== undefined) {
        document.getElementById('hostRelayAddress').value = data.relayAddress;
    }
    if (data.hostRelayKey !== undefined) {
        document.getElementById('hostRelayKey').value = data.hostRelayKey;
    }
    if (data.hostRelayJoinLink !== undefined) {
        document.getElementById('hostRelayJoinLink').value = data.hostRelayJoinLink;
    }
    if (data.relayServerName !== undefined) {
        document.getElementById('hostRelayServerName').value = data.relayServerName;
    }

    if (data.hostRelayCode) {
        document.getElementById('hostRelayCode').value = data.hostRelayCode;
    }
    var joinLink = data.hostRelayJoinLink || document.getElementById('hostRelayJoinLink').value;
    if (joinLink) {
        var codeVal = document.getElementById('hostRelayCodeValue');
        if (codeVal) codeVal.value = joinLink;
        var codeDisp = document.getElementById('hostRelayCodeDisplay');
        if (codeDisp) codeDisp.style.display = '';
    }

    if (!document.getElementById('joinRelayAddress').value) {
        document.getElementById('joinRelayAddress').value = data.relayAddress || '';
    }

    if (_pendingRelayStart) {
        _pendingRelayStart = false;
        doStartServer();
    }
}
