#pragma once

#include "types.h"

#include <functional>

class FileWatcher {
public:
  enum NotificationBits
  {
    NOTIFICATION_CREATED = 1 << 0,
    NOTIFICATION_MODIFIED = 1 << 1,
    NOTIFICATION_DELETED = 1 << 2,
    NOTIFICATION_IS_DIR = 1 << 3,
  };
  struct Notification {
    u32 event_bits;
  };

  struct FileWatchToken {
    i64 id;
  };

  using Callback = std::function<void(Notification)>;
  virtual FileWatchToken add_watch(const char* path, Callback) = 0;
  virtual void remove_watch(FileWatchToken token) = 0;

  static FileWatcher *singleton();
};
