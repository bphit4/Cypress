function parseInclusionValue(inclusion) {
    const result = {};
    if (!inclusion) {
        return result;
    }

    inclusion.split(';').forEach(part => {
        const idx = part.indexOf('=');
        if (idx <= 0) {
            return;
        }

        const key = part.slice(0, idx).trim();
        const value = part.slice(idx + 1).trim();
        if (key) {
            result[key] = value;
        }
    });

    return result;
}

function setSelectValue(id, value) {
    const el = document.getElementById(id);
    if (!el || value === undefined || value === null || value === '') {
        return;
    }

    const match = Array.from(el.options).find(option => option.value === value);
    if (match) {
        el.value = value;
        syncSegmentedGroup(id);
        updatePickerTrigger(id);
        syncPickerSelection(id);
    }
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function buildModifierTitle(name, desc) {
    const safeName = escapeHtml(name || '');
    const safeDesc = escapeHtml(desc || '');
    if (!safeDesc) {
        return '<span class="mod-title-text">' + safeName + '</span>';
    }

    return '<span class="mod-title-text">' + safeName + '</span>'
        + '<span class="mod-tooltip" tabindex="0" aria-label="' + safeDesc + '">'
        + '<span class="mod-tooltip-icon">?</span>'
        + '<span class="mod-tooltip-bubble">' + safeDesc + '</span>'
        + '</span>';
}

function escapeJs(str) {
    if (!str) return '';
    return str.replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/\r/g, '\\r').replace(/\n/g, '\\n');
}
