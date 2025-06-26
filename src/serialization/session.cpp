#include <cstdio>
#include <fmt/format.h>
#include "session.h"

namespace serialization {

FolderBasedSession::FolderBasedSession(std::filesystem::path session_folder)
  : m_folder(session_folder)
{
  std::filesystem::create_directories(session_folder / "snapshots");
  if (!std::filesystem::exists(session_folder)) {
    throw std::runtime_error("Session folder could not be found/created");
  }
}

FolderBasedSession::~FolderBasedSession() {}

// (session)
// (session)/snapshots/snap-*

void
FolderBasedSession::load()
{
  if (!std::filesystem::exists(m_folder)) {
    fmt::print("Session folder {} does not exist. Aborting session load",
               m_folder.c_str());
    return;
  }

  fmt::print("Loading session\n");

  // load meta

  // load input stream

  // load snapshots
  const auto snaps_folder = m_folder / "snapshots";
  for (const auto &entry : std::filesystem::directory_iterator(snaps_folder)) {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->load(entry);
    m_next_id = std::max(m_next_id, snapshot->get_id() + 1);
    fmt::println(" - snapshot {} id {}", entry.path().c_str(), snapshot->get_id());
    add_snapshot(std::move(snapshot));
  }
}

void
FolderBasedSession::save()
{
  if (!std::filesystem::exists(m_folder)) {
    fmt::print("Folder {}, cannot save session.", m_folder.c_str());
    return;
  }

  std::filesystem::create_directories(m_folder / "snapshots");

  // save snapshots in memory not present in folder
  for (const auto &[k, snap] : m_snapshots_by_id) {
    const auto file_path =
      fmt::format("snapshots/{}.snap", snap->get_console_timestamp_nanos());

    const auto snap_path = m_folder / file_path;
    snap->save(snap_path);
  }

  // delete old snapshots ??

  // save input stream

  // save meta
}

void
FolderBasedSession::add_snapshot(std::shared_ptr<Snapshot> snapshot)
{
  m_snapshots_by_id.insert({ snapshot->get_id(), snapshot });
}

std::shared_ptr<Snapshot>
FolderBasedSession::get_snapshot(u64 snapshot_id)
{
  return m_snapshots_by_id.at(snapshot_id);
}

u64
FolderBasedSession::get_latest_snapshot_until(u64 not_after)
{
  // TODO : This is not efficient. Ideally the snapshot container has them in a
  // time-sorted order.
  std::shared_ptr<Snapshot> latest_snapshot = nullptr;
  for (auto &[_, snapshot] : m_snapshots_by_id) {
    const u64 snapshot_nanos = snapshot->get_console_timestamp_nanos();

    if (snapshot_nanos > not_after)
      continue;

    if (!latest_snapshot ||
        latest_snapshot->get_console_timestamp_nanos() < snapshot_nanos) {
      latest_snapshot = snapshot;
    }
  }

  return latest_snapshot ? latest_snapshot->get_id() : Snapshot::NO_PARENT;
}

std::shared_ptr<Snapshot>
FolderBasedSession::new_snapshot(u64 time_nanos, u64 parent_id)
{
  std::shared_ptr<Snapshot> snapshot =
    std::make_shared<Snapshot>(m_next_id, parent_id, time_nanos);
  m_next_id++;
  return snapshot;
}

bool
FolderBasedSession::has_snapshot(u64 snapshot_id)
{
  return m_snapshots_by_id.find(snapshot_id) != m_snapshots_by_id.end();
}

std::shared_ptr<Snapshot>
FolderBasedSession::next(u64 id)
{
  auto it = m_snapshots_by_id.find(id);
  if (it == m_snapshots_by_id.end())
    return nullptr;

  it++;

  if (it == m_snapshots_by_id.end())
    return nullptr;

  return it->second;
}

std::shared_ptr<Snapshot>
FolderBasedSession::previous(u64 id)
{
  auto it = m_snapshots_by_id.find(id);
  if (it == m_snapshots_by_id.end())
    return nullptr;

  if (it == m_snapshots_by_id.begin()) {
    return nullptr;
  }

  it--;

  return it->second;
}

size_t
FolderBasedSession::count()
{
  return m_snapshots_by_id.size();
}

}
