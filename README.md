# Zoo: A multi-system emulation platform

**Zoo** is a platform for emulating game consoles, written by `uchinokitsune` and `sh4`.
(Yes, the `sh4` alias is based on the processor from this project after working on this for
a long time.)

Zoo has been primarily designed to emulate the Sega Dreamcast, though there is
preliminary support for Sony Playstation, and also emulation of our custom fantasy
console "Dragon".

Zoo has no ambition to be "the most compatible" emulator for any system. It serves
as a tool-rich experimental platform for testing new emulator concepts. Goals of
Zoo include:
- **Deterministic execution** : Zoo can produce consistent results across operating
systems and platforms. It provides the same result every time you run it. This
ability means we can support..
- **Save/Load States for all systems** : We support save/load states for the entire
observable state of the system. We further support multiple sequential save states
and input histories over time.
- **Input Clock/Controller input sampling** : We sample and playback inputs at
predictable and deterministic times during guest execution meaning we can "play back"
player inputs exactly. 
- **Deterministic Step+Rewind Debugging** : The above capabilities allow for us to
not only step forward an instruction, but also step backward in time by an instruction. Note that feature is extremely useful for debugging but also does not
100% match deterministic execution of basic blocks unless basic blocks are restricted
to single instructions because of interrupt timing uncertainty. 
- **Sessions** : A novel concept for jumping forward and backward in time, sharing
and branching execution with others. This is a DAG of state and system inputs 
expressed as a DAG over time.

Zoo exists also as a vehicle to explore how systems work, to discover how tricky
emulation is with real software that exploits weird quirks in hardware, and to
serve as a platform for other projects (such as our emulation support for our
custom hardware system [Dragon](https://sh4.dev/dragon)).

# Zoo Components
This project consists of several components which are in various levels of maturity:

- `fox` : A memory management and JIT compilation and execution framework. Fox provides
  the necessary base components to describe interacting modules with read/write
  capabilities, memory mapped files, and more. For JIT, fox exposes an IR, bytecode
  backend, and optimization passes. Fox also provides **x86_64 and aarch64 backends**.
  The rest of the Zoo project implements various frontend processors which JIT compile
  and can run using these backends. 
- `libzoo` : Refers to a variety of supporting components for performing emulation:
    - A **Scheduler** for deterministic scheduling of processor time slices and interrupts
    - **Logging** facilities with a fast in-memory log buffer, log levels, and 
    component filtering.
    - Basic **Controller** support for any SDL2-recognized controller.
    - Basic **SDL2** audio fed by guest systems' audio implementation.
    - **Optical disc** abstraction, allowing systems to reference sectors of data
    without caring about the underlying file format. Current support for `gdi` and
    `chd` formats. Disc abstraction also enables transparent wrapping layers, such
    as transparently making discs region-free.
    - **Cross-platform utilities** : Zoo does not depend on any OS-specific feature,
    rather a set of platform shared primitives which we strive to make functionally
    identical across operating systems.
    - **Save state capture and storage** : Provides mechanisms to save, diff, load,
    buffers of state, as well as read/write to file systems.
    - **Graphics API abstractions** : Some wrappers over Vulkan and WebGPU for 
    performing rendering in cross-platform manner.
    - **Game library/Settings Management** : Enables user settings to be stored
    locally and also list and scan supported games/media from the filesystem for 
    quick search/launch.
    - **Extensive debug tooling** which are easy to integrate with a new system.
    Examples include CPU debuggers/steppers/disassembly views, memory explorers,
    IO access visualizations, graphics debuggers, etc.
- `libzoo` Also features 4 frontend processors for fast JIT compilation and execution, all of which can run instructions in native modern Intel/AMD or ARM instructions:
    - `ARM7DI` : Currently used to emulate the Sega Dreamcast AICA audio subsystem
    - `r3000`  : Used to emulate instructions for the Sony PS1 CPU and GTE coprocessor.
    - `rv32`   : A modular frontend supporting RiscV 32-bit processor along with
    a configurable set of I, M, and other addon ISA extensions. Primarily tested
    to emulate our custom RV32 core in the Dragon project. 
    - `sh4`    : A full SuperH4 frontend which is used to emulate the primary
    CPU in the Sega Dreamcast
- A very simple OpenGL 3 immediate-mode implementation of the Sega Dreamcast PVR2
GPU which is sufficient for running many Dreamcast games. 
    - A *much* more complete "simulation"-level PVR2 implementation backed by 
    WebGPU is also in-development.

## System emulation concepts
- Zoo generally has one "target" executable program for each system, i.e. `penguin`
for Sega Dreamcast, `snake` for Sony Playstation, `dragon` for the Dragon project.
- Every system being emulated is referred to as the **guest** while the computer
zoo is running on is called the **host**. Similarly **guest processors** from the 
system being emulated have a **frontend** ISA which is the original instructions
from that system and a **backend** which refers to the mode of execution on the 
**host system** (i.e. your computer). Almost always the backend for execution is
native instructions for the processor you're running on.
- Each target program glues together components and system description and memory
mappings for the target system. 
- A system generally is composed of ticking a **Scheduler** which is a priority queue
of time slices to advance the state of the system. Events may represent the execution
of an interrupt or some physical process (such as diplay vsync). The scheduler timing
is expressed in terms of absolute nanoseconds since system reset. For components
which run 'forever', such as a CPU, a time slice of CPU may execute, and then the
end of running that time slice of N nanoseconds, the code will schedule itself to run again N nanosecond later in "guest time".
- All system state is wrapped up in the state of the components (RAM, internal guest
registers, etc.). Advancing state is a combination of that internal state as well
as external inputs which are most commonly controller inputs and display vsync 
interrupts. External inputs such as controller data is recorded to an input timeline
so it can be replayed back from an old save state, enabling deterministic playback
of a playthrough. This also enables going back to an old save state and changing
inputs (ala Tool-Assisted Speed runs).

## Major TODOs
- `penguin` GPU is currently 'okay' but extremely basic. This is fine for many games
to run just fine, but we would would like to complete a deterministic and 
highly-accurate "simulation" GPU implementation which mimics the PVR2 pipeline
much more closely. This can naturally handle some kinds of effects which are 
otherwise difficult to model with a simpler implementation, and provide a means
to easily explore what is and is not possible on real hardware for those without
access to real hardware.
- `penguin` has some timing and other compatibility issues which the authors would
like to resolve simply because a few games we would like to play on this emulator
- `zoo` generally needs more complete graphics, settings, media library, and
other facilities. We have a great start here but more is needed.
- All GUIs are currently very "programmer"-centric and entirely imgui. ImGui is
an incredible project and has served us well, but is not a friendly or accesible
UI for general users. 

## Contributing

Zoo is currently not open for contributions. We need to think a bit more about the
appropriate license for this work. If you are interested in contributing or publishing
bug fixes, please open an issue.

## Dependencies (MacOS)
```
brew install cmake fmt vulkan-headers
```

## Dependencies (Linux)
TODO: For now, please take a look at the included docker images which we use to 
test in CI before we perform our own merges.

## Building

Zoo years ago could build for the Windows platform. We do not believe it is an 
enormous lift to put back in support for Windows, but neither of the authors has
used Windows for years, so we do not support building/running on Windows now. (Fox
still has some remnants of early JIT support, e.g. segfault handlers, etc.)

Zoo allows for two completely different build methods. `tup` is used for Linux builds
and `cmake` supports builds on Linux and MacOS.

Dependencies are tracked as git submodules, so you need to first build the third-party components, then penguin itself:
```
make  -Cthird_party/wgpu-native lib-native
cmake -Bbuild .
make  -Cbuild -j24 
```

Alternatively using tup (Linux-only):
```
make third_party
make all
```

## Building CI/Linux from MacOS
The CI uses docker to build for Fedora 39 and runs a subset of the tests. When developing on MacOS, you probably want
to be able to test that the CI will pass before pushing. This is necessary because the MacOS SDK and GCC/Clang in Fedora
have different sets of warnings etc. 

To test locally, you need docker installed. First, from the repo folder, build the CI image
```
(cd tools; docker build --platform linux/amd64 -t penguin:build .)
```

Then you can run the build container 
```
./tools/build_fedora_gcc.sh
```

This will give you a shell equivalent to the CI build environment. If this is your first time, you need to run `make third_party`. The results for `third_party` are cached across container runs. After that has succeeded, you must run `tup` to build zoo/fox/penguin. `tup` results are not currently cached.

## Using Address Sanitizer

Zoo can be compiled and linked with [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) and [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html). Running afterwards can help reveal use-after-free, container overflows, and much more.

## Using Tracy

Zoo can be built with [Tracy](https://github.com/wolfpld/tracy) support. The full usage and integration manual for Tracy are available on the Tracy repo.

## Where is all the history?

Zoo has a very very long history as a side project for the two authors up to the time
of this writing. The log has interspersed many side projects and some personal details
which have been sanitized and squashed to a single commit to allow for an initial
public release.
