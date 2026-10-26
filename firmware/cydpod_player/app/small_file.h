#pragma once
// The Arduino VFS default is 4 KiB in addition to FatFs's mount-time FIL cache.
// Resize before the first I/O; a failure preserves the usable default buffer.
#include <SD.h>
inline File openSmallFile(const String& path, const char* mode = "r") {
  File f = SD.open(path, mode);
  if (f && !f.isDirectory() && !f.setBufferSize(512))
    Serial.println("file: 512 B buffer refused, default kept");
  return f;
}
