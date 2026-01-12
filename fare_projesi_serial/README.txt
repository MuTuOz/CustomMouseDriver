## SERIAL MOUSE GESTURE DRIVER (LINUX KERNEL MODULE)

PROJECT DESCRIPTION:
This project is a custom character device driver developed for legacy Serial Port (RS-232) mice, running at the Linux Kernel level.

The driver reads raw data coming from the standard Serial Port (typically COM1 / 0x3F8) and decodes the Microsoft Mouse Protocol (3-Byte Packet). By tracking mouse movements, it detects predefined custom patterns (gestures) and sends a signal to User Space when these gestures are performed.

FEATURES:

1. Microsoft Mouse Protocol Decoder: Processes 3-byte serial mouse packets.
2. Direct Hardware Access: Provides direct access to serial port registers (I/O Port 0x3F8).
3. Gesture Recognition: Detects special directional movements while the right mouse button is held.
4. Click Counter: Counts fast clicks (Double/Triple click).

IMPORTANT NOTE (HARDWARE):
This driver works only with mice connected via a Serial Port (RS-232) or mice emulated as a Serial Port.
It does not work with USB mice.

REQUIREMENTS:
The following packages are required to build and run the project:

* GCC Compiler
* Linux Kernel Headers
* mpg123 (to play audio files)

Installation Command (Ubuntu):
sudo apt update && sudo apt install build-essential linux-headers-$(uname -r) mpg123

INSTALLATION STEPS:

1. CONFIGURATION:
   To allow the demo application (listener.c) to run, the path to the music file must be specified.
   Open the 'listener.c' file with a text editor.
   Edit the MP3 path in the 'play_music' function according to your system.

   Example: #define MP3_PATH "/home/user/Downloads/test.mp3"

2. BUILD:
   Open a terminal in the project directory and run the 'make' command:
   make

   After a successful build, the file 'serial_mouse_pattern.ko' will be generated.

3. LOADING THE DRIVER:
   Note: If the default serial port driver is active on the system, a conflict may occur.
   To insert the module into the kernel (secure boot must be disabled):
   sudo insmod serial_mouse_pattern.ko

   To verify loading:
   dmesg | tail

4. BUILDING THE LISTENER APPLICATION:
   To compile the user-space demo application:
  gcc listener.c -o serial_listener

USAGE GUIDE:

To start the application (requires root privileges):
sudo ./serial_listener

DEFINED GESTURES:

[1] START / SWITCH COMMAND:
To start the music, perform the following combination:

```
1. Press and hold the RIGHT mouse button (do not release).
2. Drag -> RIGHT
3. Drag -> LEFT
4. Drag -> UP
5. Drag -> DOWN

The message "ŞİFRE DOĞRU! MÜZİK BAŞLIYOR" appears in the terminal and the music starts playing.
```

[2] STOP COMMAND:
To stop the music:
- Quickly click the right mouse button 3 times.

REMOVAL:
To remove the driver from the system and release memory/ports:
sudo rmmod serial_mouse_pattern
