#include "ini_parser.hpp"

static inline bool _isSpace(const char c) {
    return (c==' '||c=='\t'||c=='\r'||c=='\n');
}

String IniFile::_trim(const String& s) {
    int i=0, j=s.length()-1;
    while (i<=j && _isSpace(s[i])) i++;
    while (j>=i && _isSpace(s[j])) j--;
    if (i>j) return String();
    return s.substring(i, j+1);
}

void IniFile::_splitKV(const String& line, String& key, String& value) {
    int eq = line.indexOf('=');
    if (eq < 0) { key = _trim(line); value = ""; return; }
    key   = _trim(line.substring(0, eq));
    value = _trim(line.substring(eq+1));
}

IniFile::Section* IniFile::_findSection(const String& name) {
    for (auto &s : _tbl) if (s.name == name) return &s;
    return nullptr;
}
const IniFile::Section* IniFile::_findSection(const String& name) const {
    for (auto &s : _tbl) if (s.name == name) return &s;
    return nullptr;
}
String* IniFile::_findKey(Section& sec, const String& key) {
    for (auto &p : sec.kv) if (p.key == key) return &p.value;
    return nullptr;
}
const String* IniFile::_findKey(const Section& sec, const String& key) const {
    for (auto &p : sec.kv) if (p.key == key) return &p.value;
    return nullptr;
}

bool IniFile::load(fs::FS &fs, const char* path) {
    _tbl.clear();
    File f = fs.open(path, "r");
    if (!f) return false;

    String section;
    while (f.available()) {
        String raw = f.readStringUntil('\n');
        raw.trim();
        int sc = raw.indexOf(';');
        int hc = raw.indexOf('#');
        int cpos = -1;
        if (sc>=0 && hc>=0) cpos = min(sc, hc);
        else cpos = max(sc, hc);
        String line = (cpos>=0) ? raw.substring(0, cpos) : raw;
        line = _trim(line);
        if (line.length()==0) continue;

        if (line.startsWith("[") && line.endsWith("]")) {
            section = _trim(line.substring(1, line.length()-1));
            if (!_findSection(section)) {
                Section s; s.name = section; _tbl.push_back(s);
            }
            continue;
        }

        String key, val;
        _splitKV(line, key, val);
        if (key.length()==0) continue;

        Section* sec = _findSection(section);
        if (!sec) {
            Section s; s.name = section; _tbl.push_back(s);
            sec = &_tbl.back();
        }
        String* pval = _findKey(*sec, key);
        if (pval) *pval = val;
        else sec->kv.push_back({key, val});
    }
    f.close();
    return true;
}

bool IniFile::save(fs::FS &fs, const char* path, const String& headerComment) {
    File f = fs.open(path, "w");
    if (!f) return false;

    if (headerComment.length()) {
        f.printf("; %s\n\n", headerComment.c_str());
    }

    for (auto &sec : _tbl) {
        f.printf("[%s]\n", sec.name.c_str());
        for (auto &kv : sec.kv) {
            f.printf("%s=%s\n", kv.key.c_str(), kv.value.c_str());
        }
        f.print("\n");
    }

    f.close();
    return true;
}

bool IniFile::has(const String& section, const String& key) const {
    auto sec = _findSection(section);
    if (!sec) return false;
    return _findKey(*sec, key) != nullptr;
}

String IniFile::get(const String& section, const String& key, const String& def) const {
    auto sec = _findSection(section);
    if (!sec) return def;
    auto p = _findKey(*sec, key);
    return p ? *p : def;
}

long IniFile::getInt(const String& section, const String& key, long def) const {
    String v = get(section, key, "");
    if (!v.length()) return def;
    return strtol(v.c_str(), nullptr, 0);
}

double IniFile::getDouble(const String& section, const String& key, double def) const {
    String v = get(section, key, "");
    if (!v.length()) return def;
    return strtod(v.c_str(), nullptr);
}

bool IniFile::getBool(const String& section, const String& key, bool def) const {
    String v = get(section, key, "");
    if (!v.length()) return def;
    String t = v; t.toLowerCase();
    if (t=="1"||t=="true"||t=="yes"||t=="on") return true;
    if (t=="0"||t=="false"||t=="no"||t=="off") return false;
    return def;
}

void IniFile::set(const String& section, const String& key, const String& value) {
    Section* sec = _findSection(section);
    if (!sec) {
        Section s; s.name = section; _tbl.push_back(s);
        sec = &_tbl.back();
    }
    String* p = _findKey(*sec, key);
    if (p) *p = value;
    else sec->kv.push_back({key, value});
}

void IniFile::setInt(const String& section, const String& key, long value) {
    set(section, key, String(value));
}

void IniFile::setDouble(const String& section, const String& key, double value) {
    char buf[32]; dtostrf(value, 0, 2, buf);
    set(section, key, String(buf));
}

void IniFile::setBool(const String& section, const String& key, bool value) {
    set(section, key, value ? "true" : "false");
}
