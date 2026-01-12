## USB MOUSE GESTURE DRIVER (LINUX KERNEL MODULE)

This is an experimental USB mouse driver running on the Linux Input Subsystem.

The driver transforms a standard USB mouse into a smart control device capable of detecting custom hand movements (gestures). It listens to mouse movements in the background without blocking the mouse cursor and sends signals to User Space when predefined patterns are detected. In this example, the gestures are used to control music playback.

FEATURES:

1. Pattern Recognition: Detects special directional movements while the right mouse button is held.
2. Click Counter: Counts fast clicks (Double/Triple click).
3. Non-Blocking: Does not interfere with normal mouse cursor usage.
4. Char Device Interface: Provides easy data reading via `/dev/mousepattern`. 

REQUIREMENTS:
The following packages are required to build and run the project:

* GCC Compiler (build-essential)
* Linux Kernel Headers (linux-headers-generic)
* mpg123 (to play audio in the demo application)

Installation Command (Ubuntu/Debian):
 sudo apt update && sudo apt install build-essential linux-headers-$(uname -r) mpg123

INSTALLATION STEPS:

1. CONFIGURATION (Important):
   To allow the demo application (listener.c) to run, the path to the music file must be specified.
   Open the `listener.c` file and edit the MP3 path in the `play_music` function according to your system.
   (If not changed, the default music file included in the source will be played.)

   Example: `#define MP3_PATH "/home/user/Downloads/test.mp3"`

2. BUILD:
   Open a terminal in the project directory and run the `make` command:
    make

   After a successful build, the file `usb_mouse_pattern.ko` will be generated.

3. LOADING THE DRIVER:
   Insert the compiled module into the kernel:
    sudo insmod usb_mouse_pattern.ko

   To verify loading:
    dmesg | tail
   (The output should include a message such as "driver yuklendi, haziriz".)

4. BUILDING THE LISTENER APPLICATION:
   To compile the user-space demo application:
    gcc listener.c -o gesture_listener

USAGE GUIDE:

To start the application (requires root privileges):
 sudo ./gesture_listener

DEFINED GESTURES:

[1] START / SWITCH COMMAND:
To start the music, perform the following combination:

```
1. Press and hold the RIGHT mouse button.
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
To remove the driver from the system and free memory:
 sudo rmmod usb_mouse_pattern

NOTES:
* On systems with Secure Boot enabled, loading unsigned kernel modules may fail.
  If necessary, Secure Boot should be disabled from the BIOS.
