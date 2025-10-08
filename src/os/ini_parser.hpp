#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

class IniFile {
public:
    struct Pair { String key; String value; };
    struct Section { String name; std::vector<Pair> kv; };
    using Table = std::vector<Section>;

    bool load(fs::FS &fs, const char* path);
    bool save(fs::FS &fs, const char* path, const String& headerComment = "");

    bool   has(const String& section, const String& key) const;
    String get(const String& section, const String& key, const String& def = "") const;
    long   getInt(const String& section, const String& key, long def = 0) const;
    double getDouble(const String& section, const String& key, double def = 0.0) const;
    bool   getBool(const String& section, const String& key, bool def = false) const;

    void   set(const String& section, const String& key, const String& value);
    void   setInt(const String& section, const String& key, long value);
    void   setDouble(const String& section, const String& key, double value);
    void   setBool(const String& section, const String& key, bool value);

    const Table& table() const { return _tbl; }

private:
    static String _trim(const String& s);
    static void   _splitKV(const String& line, String& key, String& value);
    Section*      _findSection(const String& name);
    const Section* _findSection(const String& name) const;
    String*       _findKey(Section& sec, const String& key);
    const String* _findKey(const Section& sec, const String& key) const;

    Table _tbl;
};
