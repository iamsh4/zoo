#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include "shared/types.h"
#include "storage.h"

namespace serialization {

struct SnapshotMetadata {
  u64 id;
  u64 parent_id;
  u64 console_time_nanoseconds;
};

class Session {
public:
  virtual ~Session() {}

  virtual void save() = 0;
  virtual void load() = 0;

  /* Create a new snapshot compatible with this session*/
  virtual std::shared_ptr<Snapshot> new_snapshot(u64 time_nanos, u64 parent_id) = 0;

  /* Add a snapshot which is not already present in this session. */
  virtual void add_snapshot(std::shared_ptr<Snapshot> snapshot) = 0;

  virtual u64 get_latest_snapshot_until(u64 time_nanos) = 0;

  virtual bool has_snapshot(u64 snapshot_id) = 0;

  virtual std::shared_ptr<Snapshot> get_snapshot(u64 snapshot_id) = 0;

  virtual std::shared_ptr<Snapshot> next(u64 id) = 0;
  virtual std::shared_ptr<Snapshot> previous(u64 id) = 0;
  virtual size_t count() = 0;
};

/**! Session is a container for penguin emulator state, including the history of save
 * state snapshots, input history, and (eventually) debug breakpoints, etc.
 *
 * Sessions can be serialized to a file and later be loaded in full, or partially. */
class FolderBasedSession : public Session {
public:
  FolderBasedSession(std::filesystem::path session_folder);
  ~FolderBasedSession();

  void save() override;
  void load() override;

  bool has_snapshot(u64 snapshot_id) override;

  /**! Add a save state Snapshot object to the session. */
  void add_snapshot(std::shared_ptr<Snapshot> snapshot) override;

  /**! Retrieve a snapshot with the given id. If the id is not present, an exception will
   * be thrown. From a leaf Snapshot, you can get parent ids to travel back in time to
   * previous snapshots. */
  std::shared_ptr<Snapshot> get_snapshot(u64 snapshot_id) override;

  /**! Retrieve the snapshot within this session which has the highest cycle count. If no
   * snapshots are present in this session, a nullptr is returned. The returned snapshot
   * will not have a cycle count greater than 'not_after' */
  u64 get_latest_snapshot_until(u64 time_nanos = UINT64_MAX) override;

  std::shared_ptr<Snapshot> new_snapshot(u64 time_nanos, u64 parent_id) override;

  std::shared_ptr<Snapshot> next(u64 id) override;
  std::shared_ptr<Snapshot> previous(u64 id) override;
  size_t count() override;

private:
  std::filesystem::path m_folder;

  u64 m_next_id = 0;

  /**! In-memory map of Snapshots (full and partial) keyed on their id. */
  std::map<u32, std::shared_ptr<Snapshot>> m_snapshots_by_id;
};

} // namespace serialization
