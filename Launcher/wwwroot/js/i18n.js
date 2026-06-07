// i18n translation system
// usage: t('key') or t('key', {var: value}) for {var} interpolation
// html: add data-i18n="key" on elements whose textContent should be translated
//       add data-i18n-placeholder="key" for input placeholders
//       add data-i18n-title="key" for title attributes
//       add data-i18n-tip="key" for data-tip tooltip attributes

window._i18nStrings = {};
window._i18nLangMeta = {};

window.t = function(key, vars) {
    var str = window._i18nStrings[key];
    if (str === undefined) return key;
    if (vars) {
        str = str.replace(/\{(\w+)\}/g, function(_, k) {
            return vars[k] !== undefined ? String(vars[k]) : '{' + k + '}';
        });
    }
    return str;
};

function onTranslationsList(data) {
    var select = document.getElementById('languageSelect');
    if (!select || !data.langs) return;
    // migrate old underscore-format locale codes to BCP 47 (e.g. zh_hk -> zh-HK, es_419 -> es-419)
    var _storedLang = localStorage.getItem('cypress_lang');
    if (_storedLang && _storedLang.indexOf('_') !== -1) {
        var _parts = _storedLang.split('_');
        var _normalized = _parts[0].toLowerCase() + '-' + (/^\d+$/.test(_parts[1]) ? _parts[1] : _parts[1].toUpperCase());
        localStorage.setItem('cypress_lang', _normalized);
    }
    var current = localStorage.getItem('cypress_lang') || 'en-US';
    select.innerHTML = '';
    data.langs.forEach(function(item) {
        var lang   = typeof item === 'string' ? item : item.lang;
        var name   = (typeof item === 'object' && item.name)   ? item.name   : lang;
        var author = (typeof item === 'object') ? (item.author || '') : '';
        window._i18nLangMeta[lang] = { name: name, author: author };
        var opt = document.createElement('option');
        opt.value = lang;
        opt.textContent = name + ' (' + lang + ')';
        if (lang === current) opt.selected = true;
        select.appendChild(opt);
    });
    if (typeof renderPickerOptions === 'function') renderPickerOptions('languageSelect');
    _updateLangAuthorHint(current);
}

function onLanguageChanged(lang) {
    if (!lang) return;
    localStorage.setItem('cypress_lang', lang);
    send('getTranslations', { lang: lang });
    _updateLangAuthorHint(lang);
}

function _normalizeLangKey(lang) {
    if (!lang || lang.indexOf('_') === -1) return lang;
    var parts = lang.split('_');
    return parts[0].toLowerCase() + '-' + (/^\d+$/.test(parts[1]) ? parts[1] : parts[1].toUpperCase());
}

function _formatAuthor(author) {
    if (!author) return '';
    if (Array.isArray(author)) return author.join(', ');
    return String(author);
}

function _updateLangAuthorHint(lang) {
    var hint = document.getElementById('langAuthorHint');
    if (!hint) return;
    var key = _normalizeLangKey(lang);
    var cached = window._i18nLangMeta[key] || window._i18nLangMeta[lang];
    var author = _formatAuthor(cached ? cached.author : '');
    if (author) {
        hint.textContent = t('profile.translation_by') + ' ' + author;
        hint.style.display = '';
    } else {
        hint.style.display = 'none';
    }
}

function applyDomTranslations() {
    document.body.style.visibility = '';
    document.querySelectorAll('[data-i18n]').forEach(function(el) {
        var key = el.getAttribute('data-i18n');
        var val = window._i18nStrings[key];
        if (val !== undefined) {
            el.textContent = val;
            el.dir = 'auto';
        }
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach(function(el) {
        var key = el.getAttribute('data-i18n-placeholder');
        var val = window._i18nStrings[key];
        if (val !== undefined) el.placeholder = val;
    });
    document.querySelectorAll('[data-i18n-title]').forEach(function(el) {
        var key = el.getAttribute('data-i18n-title');
        var val = window._i18nStrings[key];
        if (val !== undefined) el.title = val;
    });
    document.querySelectorAll('[data-i18n-tip]').forEach(function(el) {
        var key = el.getAttribute('data-i18n-tip');
        var val = window._i18nStrings[key];
        if (val !== undefined) el.setAttribute('data-tip', val);
    });

    // rerender picker uis whose option elements carry data-i18n translations,
    // since renderPickerOptions runs at init before translations arrive
    // (this is a bit hacky i know)
    if (typeof renderPickerOptions === 'function') {
        var translatedSelects = new Set();
        document.querySelectorAll('select option[data-i18n]').forEach(function(opt) {
            var select = opt.closest('select');
            if (select && select.id) translatedSelects.add(select.id);
        });
        translatedSelects.forEach(function(id) { renderPickerOptions(id); });
    }
    if (typeof addPlaylistEntry === 'function') {
        var plEntries = document.getElementById('plEntries');
        if (plEntries && !plEntries.querySelector('.pl-entry')) {
            addPlaylistEntry();
        }
    }

    if (typeof updateGameLibrarySection === 'function') updateGameLibrarySection();
    document.querySelectorAll('.update-btn:not([disabled])').forEach(function(btn) {
        btn.textContent = t('updates.update_btn');
    });
    document.querySelectorAll('.update-dismiss-btn').forEach(function(btn) {
        btn.textContent = t('updates.later');
    });

    // re-render author hint with now-translated "Translation by" string
    var _loadedMeta = window._i18nStrings && window._i18nStrings['_meta'];
    if (_loadedMeta && _loadedMeta.lang) _updateLangAuthorHint(_loadedMeta.lang);
}
