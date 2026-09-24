#include "hw/Storage.h"

#include <FS.h>
#include <LittleFS.h>

namespace storage {

static bool s_mounted = false;

bool begin() {
    if (s_mounted) return true;
    // format-on-fail = true: first boot on a fresh board has no filesystem yet.
    s_mounted = LittleFS.begin(true);
    return s_mounted;
}

bool loadBlob(const char* path, void* out, size_t len) {
    if (!begin()) return false;
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return false;
    if (f.size() != len) {
        f.close();
        return false;
    }
    size_t got = f.read(static_cast<uint8_t*>(out), len);
    f.close();
    return got == len;
}

bool saveBlob(const char* path, const void* data, size_t len) {
    if (!begin()) return false;
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) return false;
    size_t put = f.write(static_cast<const uint8_t*>(data), len);
    f.close();
    return put == len;
}

void remove(const char* path) {
    if (!begin()) return;
    if (LittleFS.exists(path)) LittleFS.remove(path);
}

bool loadText(const char* path, String& out) {
    out = "";
    if (!begin()) return false;
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return false;
    out.reserve(f.size());
    while (f.available()) out += (char)f.read();
    f.close();
    return true;
}

bool saveText(const char* path, const String& data) {
    if (!begin()) return false;
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) return false;
    size_t put = f.write((const uint8_t*)data.c_str(), data.length());
    f.close();
    return put == data.length();
}

}  // namespace storage
