function setGameDir(path) {
    const d = document.getElementById('gameDirDisplay');
    d.textContent = path || 'No directory set';
    d.title = path || '';
}
function selectGameDirectory() { send('selectGameDir', {}); }
function autoFindDirectory() { send('autoFindDir', {}); }

document.getElementById('useModsCheckbox').addEventListener('change', function () {
    document.getElementById('modPackSelect').disabled = !this.checked;
    if (this.checked) send('getModPacks', {});
    updateModStatusPill();
});

function updateModStatusPill() {
    var pill = document.getElementById('modStatusPill');
    var label = document.getElementById('modStatusLabel');
    if (!pill || !label) return;
    var modsOn = document.getElementById('useModsCheckbox').checked;
    pill.classList.toggle('mods-on', modsOn);
    label.textContent = modsOn ? 'Mods Enabled' : 'Vanilla';
}

function togglePlaylist() {
    const checked = document.getElementById('usePlaylist').checked;
    document.getElementById('playlistSelect').disabled = !checked;
    const trigger = document.getElementById('playlistSelectTrigger');
    if (trigger) trigger.disabled = !checked;
    if (checked) send('getPlaylists', {});
}

var _launchPending = false;

function joinServer() {
    if (_launchPending) return;
    _launchPending = true;
    setTimeout(function() { _launchPending = false; }, 3000);
    // if the selected browser server has a password and none was entered, prompt
    if (typeof selectedBrowserKey !== 'undefined' && selectedBrowserKey) {
        var srv = typeof findBrowserServer === 'function' ? findBrowserServer(selectedBrowserKey) : null;
        if (srv && srv.hasPassword && !document.getElementById('serverPassword').value) {
            showPasswordModal(selectedBrowserKey);
            return;
        }
    }
    parseRelayLink('join');
    const joinMode = document.getElementById('joinRelayMode').value;
    const serverIPVal = joinMode === 'Relay' ? '' : document.getElementById('serverIP').value;
    // include gamePort from cache if we have it, so the launcher can pass the right port
    var cachedStatus = typeof serverStatusCache !== 'undefined' ? serverStatusCache[serverIPVal] : null;
    var gamePort = cachedStatus && cachedStatus.gamePort ? cachedStatus.gamePort : 0;
    // also check gamePort stashed from browser click (cache key includes side channel port, IP field doesn't)
    if (!gamePort && typeof window._selectedBrowserGamePort !== 'undefined' && window._selectedBrowserGamePort)
        gamePort = window._selectedBrowserGamePort;
    send('join', {
        username: document.getElementById('username').value,
        serverIP: serverIPVal,
        gamePort: gamePort,
        joinConnectionMode: joinMode,
        joinRelayPreset: document.getElementById('joinRelayPreset').value,
        joinRelayAddress: document.getElementById('joinRelayAddress').value,
        joinRelayKey: document.getElementById('joinRelayKey').value,
        serverPassword: document.getElementById('serverPassword').value,
        fov: document.getElementById('fov').value,
        additionalArgs: document.getElementById('additionalArgs').value,
        useMods: document.getElementById('useModsCheckbox').checked,
        modPack: document.getElementById('modPackSelect').value,
        game: getGame()
    });
}

function getLoadscreenOverrides() {
    if (getGame() !== 'GW2') return {};
    const levelId = document.getElementById('levelPicker').value;
    const modeId = document.getElementById('modePicker').value;
    const key = levelId + '+' + modeId;
    return GW2_LOADSCREEN_OVERRIDES[key] || {};
}

function toggleVpnFields() {
    var el = document.getElementById('vpnFieldsContainer');
    if (el) el.style.display = document.getElementById('hostVpnEnabled').checked ? '' : 'none';
}

function onEmancipationToggled() {
    const on = document.getElementById('emancipated').checked;
    const group = document.getElementById('centralizedFeaturesGroup');
    if (!group) return;
    group.style.opacity = on ? '0.35' : '';
    group.style.pointerEvents = on ? 'none' : '';
}

function startServer() {
    if (_launchPending) return;
    _launchPending = true;
    setTimeout(function() { _launchPending = false; }, 3000);
    // if EU relay is on, get a fresh lease first then start
    var euRelay = document.getElementById('hostUseEuRelay');
    if (euRelay && euRelay.checked) {
        requestRelayLeaseAndStart();
        return;
    }
    doStartServer();
}

function doStartServer() {
    const argsField = document.getElementById('serverArgsPreview');
    const serverArgs = argsField ? argsField.value : '';
    const lsOverrides = getLoadscreenOverrides();
    // ensure relay address is set when EU relay is on
    var euChecked = document.getElementById('hostUseEuRelay');
    if (euChecked && euChecked.checked && !document.getElementById('hostRelayAddress').value) {
        document.getElementById('hostRelayAddress').value = 'relay.v0e.dev:25200';
    }
    send('startServer', {
        deviceIP: document.getElementById('deviceIP').value,
        hostConnectionMode: document.getElementById('hostRelayMode').value,
        hostRelayPreset: document.getElementById('hostRelayPreset').value,
        hostRelayAddress: document.getElementById('hostRelayAddress').value,
        hostRelayKey: document.getElementById('hostRelayKey').value,
        hostRelayServerName: document.getElementById('hostRelayServerName').value,
        hostRelayJoinLink: document.getElementById('hostRelayJoinLink').value,
        hostRelayCode: document.getElementById('hostRelayCode').value,
        level: document.getElementById('level').value,
        inclusion: document.getElementById('inclusion').value,
        startPoint: document.getElementById('startPoint').value,
        dedicatedPassword: document.getElementById('dedicatedPassword').value,
        playerCount: document.getElementById('playerCount').value,
        usePlaylist: document.getElementById('usePlaylist').checked,
        playlist: document.getElementById('playlistSelect').value,
        allowAIBackfill: document.getElementById('allowAIBackfill').checked,
        serverAdditionalArgs: serverArgs,
        useMods: document.getElementById('useModsCheckbox').checked,
        modPack: document.getElementById('modPackSelect').value,
        game: getGame(),
        serverName: getMotdRaw(),
        serverIcon: document.getElementById('serverIconData').value,
        listedInBrowser: document.getElementById('listedInBrowser').checked,
        emancipated: document.getElementById('emancipated').checked,
        blockIdNames: document.getElementById('blockIdNames').checked,
        requireCypressAccount: document.getElementById('requireCypressAccount').checked,
        allowGlobalMods: document.getElementById('allowGlobalMods').checked,
        modpackUrl: document.getElementById('modpackUrl').value,
        vpnType: document.getElementById('hostVpnEnabled').checked ? document.getElementById('hostVpnType').value : '',
        vpnNetwork: document.getElementById('hostVpnEnabled').checked ? document.getElementById('hostVpnNetwork').value : '',
        vpnPassword: document.getElementById('hostVpnEnabled').checked ? document.getElementById('hostVpnPassword').value : '',
        loadScreenGameMode: lsOverrides.mode || '',
        loadScreenLevelName: lsOverrides.name || '',
        loadScreenLevelDescription: lsOverrides.desc || '',
        loadScreenUIAssetPath: lsOverrides.asset || ''
    });
}

function loadUserData(data) {
    if (data.game) {
        isApplyingBackendState = true;
        selectGame(data.game);
        isApplyingBackendState = false;
    }
    if (data.detectedDeviceIP !== undefined) {
        detectedDeviceIP = data.detectedDeviceIP || '';
    }
    if (data.username !== undefined) document.getElementById('username').value = data.username;
    if (data.fov !== undefined) {
        document.getElementById('fov').value = data.fov;
    }
    if (data.additionalArgs !== undefined) document.getElementById('additionalArgs').value = data.additionalArgs;
    if (data.darkMode !== undefined && typeof applyDarkMode === 'function') applyDarkMode(!!data.darkMode);
    if (typeof syncProfileDisplay === 'function') syncProfileDisplay();
    if (data.serverIP !== undefined) document.getElementById('serverIP').value = data.serverIP;
    if (data.joinConnectionMode !== undefined) document.getElementById('joinRelayMode').value = normalizeConnectionMode(data.joinConnectionMode);
    if (data.joinRelayPreset !== undefined) document.getElementById('joinRelayPreset').value = data.joinRelayPreset;
    if (data.joinRelayAddress !== undefined) document.getElementById('joinRelayAddress').value = data.joinRelayAddress;
    if (data.joinRelayKey !== undefined) document.getElementById('joinRelayKey').value = data.joinRelayKey;
    if (data.serverPassword !== undefined) document.getElementById('serverPassword').value = data.serverPassword;
    if (data.gameDir) setGameDir(data.gameDir);
    if (data.deviceIP !== undefined) document.getElementById('deviceIP').value = data.deviceIP;
    if (data.hostConnectionMode !== undefined) {
        document.getElementById('hostRelayMode').value = normalizeConnectionMode(data.hostConnectionMode);
        var isRelay = normalizeConnectionMode(data.hostConnectionMode) === 'Relay';
        var euToggle = document.getElementById('hostUseEuRelay');
        if (euToggle) euToggle.checked = isRelay;
    }
    if (data.hostRelayPreset !== undefined) document.getElementById('hostRelayPreset').value = data.hostRelayPreset;
    if (data.hostRelayAddress !== undefined) document.getElementById('hostRelayAddress').value = data.hostRelayAddress;
    // if eu relay is on, force the canonical address so stale saved values don't stick
    var euToggleEl = document.getElementById('hostUseEuRelay');
    if (euToggleEl && euToggleEl.checked) document.getElementById('hostRelayAddress').value = 'relay.v0e.dev:25200';
    if (data.hostRelayKey !== undefined) document.getElementById('hostRelayKey').value = data.hostRelayKey;
    if (data.hostRelayServerName !== undefined) document.getElementById('hostRelayServerName').value = data.hostRelayServerName;
    if (data.hostRelayJoinLink !== undefined) document.getElementById('hostRelayJoinLink').value = data.hostRelayJoinLink;
    if (data.hostRelayCode !== undefined) {
        document.getElementById('hostRelayCode').value = data.hostRelayCode;
        if (data.hostRelayCode) {
            var codeVal = document.getElementById('hostRelayCodeValue');
            if (codeVal) codeVal.textContent = data.hostRelayCode;
            var codeDisp = document.getElementById('hostRelayCodeDisplay');
            if (codeDisp) codeDisp.style.display = '';
        }
    }
    if (data.dedicatedPassword !== undefined) document.getElementById('dedicatedPassword').value = data.dedicatedPassword;
    if (data.listedInBrowser !== undefined) document.getElementById('listedInBrowser').checked = data.listedInBrowser;
    if (data.playerCount !== undefined) document.getElementById('playerCount').value = data.playerCount;
    if (data.level) {
        let levelPickerVal = data.level;
        // GW1: saved level IDs don't include variant suffix, find matching option
        if (getGame() === 'GW1') {
            const el = document.getElementById('levelPicker');
            const match = el ? Array.from(el.options).find(o => o.value === data.level || o.value.startsWith(data.level + '#')) : null;
            if (match) levelPickerVal = match.value;
        }
        setSelectValue('levelPicker', levelPickerVal);
        document.getElementById('level').value = data.level;
    }
    if (data.inclusion) {
        document.getElementById('inclusion').value = data.inclusion;
        const inclusionParts = parseInclusionValue(data.inclusion);
        let modeVal = inclusionParts.GameMode || '';
        // GW1: strip variant suffix (trailing digit) from mode ID for the picker
        if (getGame() === 'GW1' && modeVal && /\d$/.test(modeVal)) {
            modeVal = modeVal.replace(/\d+$/, '');
        }
        setSelectValue('modePicker', modeVal);
        setSelectValue('todPicker', inclusionParts.TOD);
        setSelectValue('hostedModePicker', inclusionParts.HostedMode);
    }
    if (data.startPoint) {
        document.getElementById('startPoint').value = data.startPoint;
        setSelectValue('startPointPicker', data.startPoint);
    }
    if (data.serverName !== undefined) setMotdRaw(data.serverName);
    if (data.modpackUrl !== undefined) document.getElementById('modpackUrl').value = data.modpackUrl;
    if (data.serverIcon) {
        document.getElementById('serverIconData').value = data.serverIcon;
        document.getElementById('serverIconPreview').innerHTML = '<img src="data:image/jpeg;base64,' + data.serverIcon + '" alt="Server Icon" draggable="false">';
        document.getElementById('clearIconBtn').style.display = '';
    }
    if (data.serverList && typeof setServerList === 'function') {
        setServerList(data.serverList);
        if (typeof pingAllServers === 'function') pingAllServers();
    }

    syncPickerCompatibility();
    onInclusionChanged();
    syncSegmentedGroup('todPicker');
    syncSegmentedGroup('hostedModePicker');
    syncRelayUi('join');
    syncRelayUi('host');
    updateDetectedDeviceIpNote();
}

function showStatus(text, level) {
    const el = document.getElementById('gameStatus');
    document.getElementById('gameStatusText').textContent = text;
    el.className = 'game-status ' + level;
    el.style.display = '';
    clearTimeout(el._t);
    el._t = setTimeout(() => el.style.display = 'none', 8000);
}

let plEntryCount = 0;
