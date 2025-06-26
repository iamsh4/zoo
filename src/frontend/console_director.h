#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <variant>
#include <optional>

#include "core/console.h"
#include "peripherals/controller.h"
#include "peripherals/keyboard.h"
#include "serialization/input_timeline.h"
#include "serialization/session.h"

/**!
 *
 * @brief The ConsoleDirector is responsible for taking the Console, which is
 *        mostly a bag of devices, and invoking user logic.
 *
 * The intended flow is:
 *
 *    User -> UI -> ConsoleDirector -> Console
 *
 * User initiates some action on the UI/keyboard/etc which passes to ConsoleDirector to
 * actually call the right logic on the Console. This also includes things like save
 * states, and the like.
 */
class ConsoleDirector {
public:
  using PluggableDevice = std::variant<maple::Controller, maple::Keyboard>;

  ConsoleDirector(std::shared_ptr<Console> console);

  void set_session(std::shared_ptr<serialization::Session> session);

  /**! Pass-through to the SH4. Switches backend for the CPU (e.g. interpreter, native,
   * etc.) */
  void set_cpu_execution_mode(cpu::SH4::ExecutionMode);

  /**! Plug in a standard gamepad on the given port, optionally with a VMU plugged-in. */
  void attach_controller(unsigned port);

  /**! Attach a VMU to the device on the associated port (e.g. into the corresponding
   * controller for that port) */
  void attach_vmu(unsigned port, std::filesystem::path vmu_flash_path);

  /**! Reset the console as if a soft reset had happened. */
  void reset_console();

  void launch_game(const std::string &disc_path);

  bool is_halted() const
  {
    return m_cpu_continue.load() == 0;
  }

  std::shared_ptr<Console> console()
  {
    return m_console;
  }

  const std::shared_ptr<Console> &console() const
  {
    return m_console;
  }

  /**! Actions and settings that the user may initiate. Some of these are triggered by
   * nicely-named member functions. TODO : Better handle those flags which cover private
   * logic (e.g. save state) and those which are more public flags (e.g. vblank
   * limiting)*/
  enum class flags : u64 {
    SAVE_STATE_PENDING,
    LOAD_STATE_PENDING,
    VBLANK_LIMITING,
  };

  void set_flag(flags flag, bool yes_no)
  {
    const u64 mask = 1llu << (u64)flag;
    m_flags = (m_flags & ~mask);
    if (yes_no) {
      m_flags |= mask;
    }
  }

  bool is_flag_set(flags flag)
  {
    const u64 mask = 1llu << (u64)flag;
    return m_flags & mask;
  }

  /**! Save a new state for the console. */
  void save_state();

  /**! Load the most recent state. If the most recent snapshot(s) is a partial snapshot,
   * apply the most recent full snapshot, then apply partial snapshots going forward. */

  void load_previous();
  void load_current();
  void load_next();

  /**! Pause or un-pause the console. */
  void pause(bool yes_no)
  {
    if (yes_no) {
      m_cpu_continue.store(0);
    } else {
      m_cpu_continue.store(INT64_MAX);
    }
  }

  /**! Toggle pausing the console. */
  void pause_toggle()
  {
    m_cpu_continue.store(m_cpu_continue.load() ? 0 : INT64_MAX);
  }

  void step_cpu(int cycles)
  {
    m_cpu_continue.store(cycles);
  }

  /**! Set whether we are currently recording or playing back inputs. */
  void set_input_state(const serialization::CompleteInputState &state)
  {
    m_input_state = state;
    m_input_mode = InputMode::LiveRecording;
  }

  /**! Launch cpu and apu threads, effectively booting the console. */
  void launch_threads();

  /**! Signal cpu and apu threads to quick, effectively turn the console power off. */
  void shutdown_threads();

  void cpu_debug_run_single_block();
  void cpu_debug_step_single_block(u64 stop_on_cycles);
  void apu_debug_run_single_block();

private:
  void cpu_thread_func();

  /**! Logic to be performed on vblank-in. */
  void vblank_in_logic();

  void apply_input_overrides();

  std::shared_ptr<Console> m_console;

  /**! Session object capturing Snapshot and Input data. */
  std::shared_ptr<serialization::Session> m_session;

  enum class InputMode {
    LiveRecording,
    Playback,
  };
  InputMode m_input_mode = InputMode::LiveRecording;

  serialization::CompleteInputState m_input_state = {};
  serialization::InputTimeline m_input_timeline;

  std::array<std::shared_ptr<maple::Controller>, 4> m_controllers;

  std::thread m_cpu_thread;

  std::atomic<i64> m_cpu_continue;
  std::atomic<bool> m_is_exiting;

  // Real-world host time that the last vblank took place. Used if vblank-limiting is
  // enabled.
  std::chrono::steady_clock::time_point m_last_guest_vblank_in;

  u64 m_flags = {};
  u64 m_save_state_count = 0;
  u64 m_current_snapshot_id = serialization::Snapshot::NO_PARENT;

  std::atomic<bool> m_threads_should_checkpoint;
  std::atomic<bool> m_cpu_barrier;

  void run_checkpoint_action(std::function<void()> func);
};
