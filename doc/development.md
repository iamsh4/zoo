
# Overview
Penguin is a Sega Dreamcast emulator for Linux, MacOS, and Windows. Development has been primarily done in Linux. 

# Hardware Documentation
Penguin's implementation is based on a number of documents which were easily found throughout the internet. 

### Renesas SH4 Documentation
During the early days of the project where we needed to stand up an SH4 CPU emulator, we found the SH4 processor documentation from Renesas (the current owners of the SuperH IP) to be invaluable. The Renesas documentation explained, in depth, the architecture, all registers and opcodes, and opcode side-effects. Especially helpful was the fact they give a C-like implementation of each opcode which concretely explains how the opcode modifies registers and flags.

### Dreamcast Hardware Documentation
There is a "Dreamcast/Dev.Box System Architecture" document which provides the vast majority on system architecture, including MMIO register locations, behaviors, memory maps, DMA mechanisms, interrupts available, a large set of chapters on graphics-related work. This emulator (and likely all other Dreamcast emulators) would not be possible without this document.

# Emulator Architecture


# Historical Notes

- Initially, penguin had no GUI, but instead had a console-based debugger, inspired by GDB, called 'penpen'. We utilized this to get the CPU up and running for a long time. Once we got to a point where we believe we should be able to draw something, we moved to building an interface in ImGUI, which was somewhat new at the time. Over time, ImGUI-based tools have been added to Penguin to support debugging and development.

- During the initial work to get the Dreamcast to boot, we implemented opcodes as necessary. We would run the firmware rom until we got to an instruction not yet implemented, then go implement it, and see if we got further. Eventually we got to infinite loops, which we expected were waiting for interrupts, which hadn't been implemented yet. After implementing interrupt logic, we began to see DMAs of display list data to the graphics system. At this point we moved to building the graphical interface for Penguin.

# Gotchas and Funny Moments

- In the early days before we ever got to any graphics, we were unable to make any progress, and there were no clear signs of incorrect opcodes or interrupts that should be firing. The processor would not make any more progress through booting though. We hadn't invested in processor exception handling at that point, because we didn't think it would be necessary, at least during boot. We later figured out that the boot firmware depends on an exception being triggered to progress. The invoked exception handler, which is installed by the firmware, returned to a new piece of code which allowed boot to continue.

- Getting through the boot process initially took a very long time, as there were many opcodes to implement and various systems that needed implementation. One very helpful resource for us was a heavily-annotated decompilation of the bootrom which gave a lot of context for various functions and what they were trying to do.

- Initially, our video signal generation was very wrong, and there are many components in the system that expect reliable interrupts and timing on this component. After we saw display lists being DMA'd for the first time, we used a rudimentary understanding of the data format that we had so far to look at the coordinate data for vertices, and saw things in the range of (0,0) to (640, 480), which we knew meant good things. Instead of jumping into a full rendering system, we tried to simply plot the locations of vertices in the window with GL_POINTS. Initially we saw a bunch of vertices which were clearly arranged as part of the retail boot animation, but nothing would move, which was dissappointing. Hours later, after letting the emulator sit for a minute, we noticed that the points were in fact moving! After fixing some other drawing issues and looking harder at the video signal generation, we discovered it was running much slower than it should -- After fixing this, we had the first animated graphical output from Penguin ever: https://imgur.com/yg0yLbG .

- For a long time, all DMAs to the TA would immediately write to a single render command queue, and then when STARTRENDER was triggered, we would render whatever was in the queue. This actually works for a lot of games (surprisingly), like Power Stone and Hydro Thunder, which basically will wait for rendering to finish before they start issuing new DMAs to the TA. The system boot animation, and many other games however will simultaneously prepare the next frame in memory by issuing TA DMAs while the current frame is still being rendered. This would cause a race condition which exhibited as flickering. After we started properly using TA_ISP_BASE to key the next TA DMAs into a queue, and then would use PARAM_BASE as a key for which queue to render, all flickering issues went away!

- We stared at weird ground textures in the opening forest area of Rayman 2 for a long time thinking they were a display list issue. They were bump map textures that we were interpreting as color data!