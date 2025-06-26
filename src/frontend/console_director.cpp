#include <fmt/core.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <memory>
#include <stack>
#include "console_director.h"
#include "media/disc.h"
#include "peripherals/vmu.h"
#include "shared/platform.h"
#include "shared/profiling.h"
#include "shared/stopwatch.h"

using namespace std::chrono_literals;
using serialization::Snapshot;

// TODO : Update stepping to handle the following cases:
// - Step CPU instruction
// - Step APU instruction
// - Step Frame
// - Run continuously (repeatedly step frame)

void
ConsoleDirector::cpu_thread_func()
{
  platform::setThreadName("Guest Simulation");

  const std::chrono::milliseconds milliseconds_per_execution(5);
  int64_t step_count = m_cpu_continue.load();
  while (true) {
    bool halt = false;

    /* Global checkpoint which director can inject logic into. */
    if (m_threads_should_checkpoint) {
      m_cpu_barrier = true;
      while (m_cpu_barrier) {
        usleep(100);
      }
    }

    if (m_is_exiting.load()) {
      return;
    }

    if (step_count == INT64_MAX) {
      /* If last "continue" was indefinite/'infinite'... */
      try {
        ProfileZoneNamed("run_cpu_5ms");
        Stopwatch stopwatch;
        const u64 start_nanos = m_console->current_time();
        const u64 host_start =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();

        m_console->run_for(std::chrono::nanoseconds(milliseconds_per_execution));

        const u64 end_nanos = m_console->current_time();
        const u64 host_end =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();

        m_console->metrics().increment(zoo::dreamcast::Metric::GuestNanos,
                                       end_nanos - start_nanos);
        m_console->metrics().increment(zoo::dreamcast::Metric::HostNanos,
                                       host_end - host_start);
      }

      catch (cpu::SH4::BreakpointException &) {
        printf("Emulator halted: SH4: Breakpoint\n");
        halt = true;
      }

      catch (std::out_of_range &e) {
        printf("Emulator halted: SH4: %s\n", e.what());
        halt = true;
      }

      catch (std::runtime_error &e) {
        printf("Emulator halted: SH4: %s\n", e.what());
        halt = true;
      }

      step_count = m_cpu_continue.load();
    } else if (step_count > 0) {
      /* Single-stepping a finite number of cycles */
      try {
        m_console->debug_step();
      } catch (cpu::SH4::BreakpointException &) {
        halt = true;
      }

      --step_count;
    } else if (step_count < 0) {
      try {
        /* TODO : Support for stepping backwards in debug */
        // m_console->debug_step_back(m_session);
        halt = true;
      } catch (cpu::SH4::BreakpointException &) {
        halt = true;
        // If we hit a breakpoint while going backwards, do nothing for now.
      }
    } else {
      /* Simulation paused */
      std::this_thread::sleep_for(1ms);

      step_count = m_cpu_continue.load();
      if (step_count > 0 && step_count != INT64_MAX) {
        m_cpu_continue.store(0);
      }
    }

    if (halt) {
      step_count = 0;
      m_cpu_continue.store(0);
    }
  }

  return;
}

void
ConsoleDirector::cpu_debug_run_single_block()
{
  m_console->debug_run_single_block();
}

void
ConsoleDirector::cpu_debug_step_single_block(u64 stop_on_cycles)
{
  m_console->debug_step_single_block(stop_on_cycles);
}

void
ConsoleDirector::apu_debug_run_single_block()
{
  if (m_console->memory()->read<u32>(0x00800000u) != 0x00000000) {
    auto aica = m_console->aica();
    aica->step_block();
  }
}

ConsoleDirector::ConsoleDirector(std::shared_ptr<Console> console)
  : m_console(console),
    m_is_exiting(false)
{
  m_cpu_continue.store(INT64_MAX); // continue.
  m_threads_should_checkpoint = false;
  m_cpu_barrier = false;
  console->set_vblank_in_callback(std::bind(&ConsoleDirector::vblank_in_logic, this));
}

void
ConsoleDirector::launch_game(const std::string &disc_path)
{
  run_checkpoint_action([&] {
    // Send console into a total reset
    m_console->power_reset();

    // Mount the new disc
    std::shared_ptr<zoo::media::Disc> disc = zoo::media::Disc::open(disc_path.c_str());
    m_console->gdrom()->mount_disc(disc);
  });
}

void
ConsoleDirector::set_session(std::shared_ptr<serialization::Session> session)
{
  m_session = session;
}

void
ConsoleDirector::set_cpu_execution_mode(cpu::SH4::ExecutionMode mode)
{
  m_console->cpu()->set_execution_mode(mode);
}

void
ConsoleDirector::run_checkpoint_action(std::function<void()> func)
{
  /* Signal both threads to block in the checkpoint area. */
  m_threads_should_checkpoint = true;

  /* Wait for each thread to respond that it's ready for the global
   * checkpoint. */
  while (!m_cpu_barrier) {
    usleep(100);
  }

  func();

  m_threads_should_checkpoint = false;
  m_cpu_barrier = false;
}

void
ConsoleDirector::save_state()
{
  run_checkpoint_action([&] {
    if (!m_session) {
      return;
    }

    const u64 current_time = m_console->current_time();
    const u64 latest_snapshot_id = m_session->get_latest_snapshot_until(current_time);
    auto new_snapshot = m_session->new_snapshot(current_time, latest_snapshot_id);

    const u64 ss_start = epoch_nanos();
    m_console->save_state(*new_snapshot);
    const u64 ss_end = epoch_nanos();

    // fmt::println("{} nanos to save state", ss_end - ss_start);
    // new_snapshot->print_snapshot_report(true);

    m_session->add_snapshot(new_snapshot);
    m_current_snapshot_id = new_snapshot->get_id();

    // TODO : don't save the session on every state
    m_session->save();

    m_save_state_count++;
  });
}

void
ConsoleDirector::load_current()
{
  run_checkpoint_action([&] {
    if (!m_session)
      return;

    // No snapshot ID loaded before, get the furthest into the timeline
    if (m_current_snapshot_id == serialization::Snapshot::NO_PARENT)
      m_current_snapshot_id = m_session->get_latest_snapshot_until(UINT64_MAX);

    fmt::println("LOAD_CURRENT {}", m_current_snapshot_id);

    if (m_session->has_snapshot(m_current_snapshot_id)) {
      auto snap = m_session->get_snapshot(m_current_snapshot_id);
      if (snap) {
        m_console->load_state(*snap);
      }
    }

    m_input_mode = InputMode::Playback;
  });
}

void
ConsoleDirector::load_next()
{
  run_checkpoint_action([&] {
    if (!m_session)
      return;

    // No snapshot ID loaded before, get the furthest into the timeline
    if (m_current_snapshot_id == serialization::Snapshot::NO_PARENT)
      m_current_snapshot_id = m_session->get_latest_snapshot_until(UINT64_MAX);

    // Get next, load
    if (auto snap = m_session->next(m_current_snapshot_id); snap) {
      m_console->load_state(*snap);
      fmt::println("LOAD_NEXT {} -> {}", m_current_snapshot_id, snap->get_id());
      m_current_snapshot_id = snap->get_id();
    }

    m_input_mode = InputMode::Playback;
  });
}

void
ConsoleDirector::load_previous()
{
  run_checkpoint_action([&] {
    if (!m_session)
      return;

    // No snapshot ID loaded before, get the furthest into the timeline
    if (m_current_snapshot_id == serialization::Snapshot::NO_PARENT)
      m_current_snapshot_id = m_session->get_latest_snapshot_until(UINT64_MAX);

    // Get next, load
    if (auto snap = m_session->previous(m_current_snapshot_id); snap) {
      fmt::println("LOAD_PREVIOS {} -> {}", m_current_snapshot_id, snap->get_id());
      m_console->load_state(*snap);
      m_current_snapshot_id = snap->get_id();
    }

    m_input_mode = InputMode::Playback;
  });
}

void
ConsoleDirector::attach_controller(unsigned port)
{
  assert(!m_controllers[port] && "Something already plugged in on that controller port.");
  m_controllers[port] = std::make_shared<maple::Controller>();
  m_console->maple_bus()->add_device(port, m_controllers[port]);
}

void
ConsoleDirector::attach_vmu(unsigned port, std::filesystem::path path)
{
  std::shared_ptr<maple::VMU> vmu = std::make_shared<maple::VMU>(path.c_str());
  m_controllers[port]->add_device(0u, vmu);
}

void
ConsoleDirector::launch_threads()
{
  m_last_guest_vblank_in = std::chrono::steady_clock::now();
  m_cpu_thread = std::thread(&ConsoleDirector::cpu_thread_func, this);
}

void
ConsoleDirector::shutdown_threads()
{
  m_is_exiting = true;
  m_cpu_thread.join();
}

void
ConsoleDirector::reset_console()
{
  assert(false && "reset unimplemented");
}

void
ConsoleDirector::vblank_in_logic()
{
  ProfileZone;

  /* If it has been requested, limit the speed that we let guest vblanks happen
   * in real time so that games that run ridiculously fast still appear a
   * reasonable speed. */
  // if (is_flag_set(flags::VBLANK_LIMITING)) {
  //   ProfileZoneNamed("VBlank Limit/Sleep") using spg_frame_duration =
  //     std::chrono::duration<int, std::ratio<1, 60>>;
  //   std::this_thread::sleep_until(m_last_guest_vblank_in + spg_frame_duration(1));
  //   m_last_guest_vblank_in = std::chrono::steady_clock::now();
  // }

  if (m_console->aica()->output()->queued_samples() > 44100 / 10) {
    const u64 host_start = epoch_nanos();

    while (m_console->aica()->output()->queued_samples() > 44100 / 10) {
      std::this_thread::yield();
    }

    const u64 host_end = epoch_nanos();
    m_console->metrics().increment(zoo::dreamcast::Metric::HostNanosLimiting,
                                   host_end - host_start);
  }

  /* Alternatively, could clear the audio queue and just move on. */

  /* SDL has already written controller states into a input snapshot. */

  /* 1a.. Save it to the timeline,
   *     OR...
   * 1b.. Reload from the timeline. */
  const u64 timestamp = m_console->get_vblank_in_count();
  if (m_input_mode == InputMode::LiveRecording) {
    apply_input_overrides();
    m_input_timeline.set(timestamp, m_input_state);
  } else if (m_input_mode == InputMode::Playback) {
    if (m_input_timeline.has(timestamp)) {
      m_input_state = m_input_timeline.get(timestamp);
    } else {
      fmt::print("was in playback, but ts {} not found, switching back to record mode\n",
                 timestamp);
      m_input_timeline.set(timestamp, m_input_state);
      m_input_mode = InputMode::LiveRecording;
    }
  } else {
    assert(false && "unhandled director input mode");
  }

  /* 2. Feed that data into the controllers themselves */
  {
    for (unsigned controller = 0; controller < 4; ++controller) {
      const auto &con_state = m_input_state.controllers[controller];
      m_controllers[controller]->joystick_x(con_state.analog_x);
      m_controllers[controller]->joystick_y(con_state.analog_y);
      m_controllers[controller]->trigger_left(con_state.trigger_left);
      m_controllers[controller]->trigger_right(con_state.trigger_right);
      for (unsigned i = 0; i < (unsigned)maple::Controller::Button::N_Buttons; ++i) {
        const auto button = (maple::Controller::Button)i;
        if (con_state.is_button_down(button)) {
          m_controllers[controller]->button_down(button);
        } else {
          m_controllers[controller]->button_up(button);
        }
      }
    }
  }

  /* TODO More work per-vblank? */
}

void
ConsoleDirector::apply_input_overrides()
{
  return;

  const u64 skip_start = 60;
  const u64 skip_end = 70;

  const u64 vblank_count = m_console->get_vblank_in_count();
  if (vblank_count >= skip_start && vblank_count < skip_end) {
    if (vblank_count < (skip_start + skip_end) / 2) {
      m_input_state.controllers[0].button_down(maple::Controller::Button::Start);
    } else {
      m_input_state.controllers[0].button_up(maple::Controller::Button::Start);
    }
  }

  // Patch over splash screen timer code
  if (vblank_count < 200) {
    for (u32 i = 0x8c0084f0; i <= 0x8c00851c; i += 2) {
      m_console->cpu()->mem_write<u16>(i, 0x0009);
    }
  }
}
