#include "sd_ota.h"

#include <SD_MMC.h>
#include <Update.h>

#include "konfiguracja.h"
#include "scan_log_sd.h"

namespace {

String entry_path(File &entry) {
  String path = entry.path();
  if (!path.length()) path = String("/") + entry.name();
  if (!path.startsWith("/")) path = String("/") + path;
  return path;
}

bool path_matches_ota_prefix(const String &path) {
  int slash = path.lastIndexOf('/');
  String name = slash >= 0 ? path.substring((unsigned int)slash + 1) : path;
  return name.startsWith(SD_OTA_FILE_PREFIX);
}

String find_ota_file_path() {
  if (!scan_log_sd_ready()) return "";

  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return "";
  }

  String best_path;
  File entry = root.openNextFile();
  while (entry) {
    String path = entry_path(entry);
    bool usable = !entry.isDirectory() && entry.size() > 0 && path_matches_ota_prefix(path);
    entry.close();

    if (usable && (!best_path.length() || path > best_path)) {
      best_path = path;
    }

    entry = root.openNextFile();
  }
  root.close();
  return best_path;
}

}  // namespace

bool sd_ota_file_available() {
  return sd_ota_file_path().length() > 0;
}

String sd_ota_file_path() {
  return find_ota_file_path();
}

String sd_ota_file_info() {
  if (!scan_log_sd_ready()) return "SD unavailable";

  String path = sd_ota_file_path();
  if (!path.length()) return String("Missing file ") + SD_OTA_FILE_PREFIX + "*";

  File f = SD_MMC.open(path.c_str(), FILE_READ);
  if (!f) return path + " does not exist";
  size_t size = f.size();
  f.close();
  return path + " (" + size + " B)";
}

bool sd_ota_apply_from_sd(String &error_out) {
  error_out = "";
  if (!scan_log_sd_ready()) {
    error_out = "SD unavailable";
    return false;
  }

  String path = sd_ota_file_path();
  if (!path.length()) {
    error_out = String("Missing file ") + SD_OTA_FILE_PREFIX + "*";
    return false;
  }

  File fw = SD_MMC.open(path.c_str(), FILE_READ);
  if (!fw) {
    error_out = String("Missing file ") + path;
    return false;
  }

  size_t fw_size = fw.size();
  if (fw_size == 0) {
    fw.close();
    error_out = "OTA file is empty";
    return false;
  }

  if (!Update.begin(fw_size)) {
    error_out = String("Update.begin: ") + Update.errorString();
    fw.close();
    return false;
  }

  size_t written = Update.writeStream(fw);
  fw.close();
  if (written != fw_size) {
    error_out = String("Written ") + written + "/" + fw_size + " B";
    Update.abort();
    return false;
  }

  if (!Update.end(true)) {
    error_out = String("Update.end: ") + Update.errorString();
    return false;
  }

  if (!Update.isFinished()) {
    error_out = "Update did not finish";
    return false;
  }

  return true;
}
