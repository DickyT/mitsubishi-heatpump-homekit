#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

class FileSystemManager {
public:
    static bool begin(bool formatOnFail = true) {
        if (mounted()) {
            return true;
        }
        mounted() = SPIFFS.begin(formatOnFail);
        return mounted();
    }

    static bool isMounted() {
        return mounted();
    }

    static String normalizePath(const String& rawPath) {
        String path = rawPath;
        path.trim();
        if (path.length() == 0) {
            return "/";
        }
        path.replace("\\", "/");
        while (path.indexOf("//") >= 0) {
            path.replace("//", "/");
        }
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        if (path.length() > 1 && path.endsWith("/")) {
            path.remove(path.length() - 1);
        }
        return path;
    }

    static bool isSafePath(const String& rawPath) {
        String path = normalizePath(rawPath);
        return path.length() > 0 &&
               path.indexOf("..") < 0 &&
               path != "/.keep";
    }

    static bool exists(const String& rawPath) {
        if (!begin()) {
            return false;
        }
        return SPIFFS.exists(normalizePath(rawPath));
    }

    static File openRead(const String& rawPath) {
        if (!begin() || !isSafePath(rawPath)) {
            return File();
        }
        return SPIFFS.open(normalizePath(rawPath), FILE_READ);
    }

    static File openWrite(const String& rawPath) {
        if (!begin() || !isSafePath(rawPath)) {
            return File();
        }
        return SPIFFS.open(normalizePath(rawPath), FILE_WRITE);
    }

    static bool remove(const String& rawPath, String& message) {
        if (!begin()) {
            message = "SPIFFS is not mounted";
            return false;
        }

        String path = normalizePath(rawPath);
        if (!isSafePath(path) || path == "/") {
            message = "invalid path";
            return false;
        }

        if (SPIFFS.exists(path)) {
            bool ok = SPIFFS.remove(path);
            message = ok ? "file deleted" : "delete failed";
            return ok;
        }

        String prefix = path + "/";
        String keepPath = prefix + ".keep";
        int childCount = 0;
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file) {
            String name = normalizePath(file.name());
            if (name.startsWith(prefix)) {
                childCount++;
                if (name != keepPath) {
                    message = "directory is not empty";
                    return false;
                }
            }
            file = root.openNextFile();
        }

        if (childCount == 1 && SPIFFS.exists(keepPath)) {
            bool ok = SPIFFS.remove(keepPath);
            message = ok ? "directory deleted" : "directory delete failed";
            return ok;
        }

        message = "path not found";
        return false;
    }

    static bool createFile(const String& rawPath, const String& content, String& message) {
        if (!begin()) {
            message = "SPIFFS is not mounted";
            return false;
        }

        String path = normalizePath(rawPath);
        if (!isSafePath(path) || path == "/") {
            message = "invalid path";
            return false;
        }

        File file = SPIFFS.open(path, FILE_WRITE);
        if (!file) {
            message = "file open failed";
            return false;
        }
        file.print(content);
        file.close();
        message = "file saved";
        return true;
    }

    static bool createDirectory(const String& rawPath, String& message) {
        if (!begin()) {
            message = "SPIFFS is not mounted";
            return false;
        }

        String dir = normalizePath(rawPath);
        if (!isSafePath(dir) || dir == "/") {
            message = "invalid directory path";
            return false;
        }
        if (SPIFFS.exists(dir)) {
            message = "file already exists at directory path";
            return false;
        }
        bool ok = createFile(dir + "/.keep", "", message);
        if (ok) {
            message = "directory created";
        }
        return ok;
    }

    static String joinPath(const String& rawDir, const String& rawName) {
        String dir = normalizePath(rawDir);
        String name = rawName;
        name.trim();
        name.replace("\\", "/");
        int slash = name.lastIndexOf('/');
        if (slash >= 0) {
            name = name.substring(slash + 1);
        }
        if (dir == "/") {
            return "/" + name;
        }
        return dir + "/" + name;
    }

    static String listJson(const String& rawDir) {
        if (!begin()) {
            return "{\"ok\":false,\"error\":\"SPIFFS is not mounted\"}";
        }

        String dir = normalizePath(rawDir);
        if (!isSafePath(dir)) {
            return "{\"ok\":false,\"error\":\"invalid directory\"}";
        }

        String prefix = dir == "/" ? "/" : dir + "/";
        String dirs = "";
        String files = "";

        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file) {
            String name = normalizePath(file.name());
            if (!name.startsWith(prefix) || name == prefix + ".keep") {
                file = root.openNextFile();
                continue;
            }

            String rest = name.substring(prefix.length());
            int slash = rest.indexOf('/');
            if (slash >= 0) {
                String childDir = prefix + rest.substring(0, slash);
                if (!jsonArrayContainsName(dirs, childDir)) {
                    appendJsonItem(dirs, childDir, "dir", 0);
                }
            } else {
                appendJsonItem(files, name, "file", file.size());
            }

            file = root.openNextFile();
        }

        String json = "{";
        json += "\"ok\":true,";
        json += "\"dir\":\"" + jsonEscape(dir) + "\",";
        json += "\"items\":[" + dirs;
        if (dirs.length() > 0 && files.length() > 0) {
            json += ",";
        }
        json += files + "]";
        json += "}";
        return json;
    }

    static String infoJson() {
        if (!begin()) {
            return "{\"mounted\":false}";
        }
        size_t total = SPIFFS.totalBytes();
        size_t used = SPIFFS.usedBytes();
        return String("{\"mounted\":true,\"totalBytes\":") + total +
               ",\"usedBytes\":" + used + ",\"freeBytes\":" + (total - used) + "}";
    }

    static String jsonEscape(const String& input) {
        String out;
        out.reserve(input.length() + 16);
        for (size_t i = 0; i < input.length(); i++) {
            char c = input[i];
            if (c == '\\' || c == '"') {
                out += '\\';
                out += c;
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out += c;
            }
        }
        return out;
    }

private:
    static bool& mounted() {
        static bool value = false;
        return value;
    }

    static bool jsonArrayContainsName(const String& json, const String& name) {
        return json.indexOf("\"name\":\"" + jsonEscape(name) + "\"") >= 0;
    }

    static void appendJsonItem(String& json, const String& name, const char* type, size_t size) {
        if (json.length() > 0) {
            json += ",";
        }
        json += "{";
        json += "\"name\":\"" + jsonEscape(name) + "\",";
        json += "\"type\":\"" + String(type) + "\",";
        json += "\"size\":" + String(size);
        json += "}";
    }
};
