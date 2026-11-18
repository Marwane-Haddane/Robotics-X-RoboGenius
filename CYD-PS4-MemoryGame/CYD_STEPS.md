# CYD + PS4: memory-combo game

Open this project folder yourself in VS Code:

`C:\1Project\some robots\prj\gta game`

This is now the actual project directory, not a junction. Source files, Arduino,
Bluepad32, BTstack, downloaded components and Git metadata are all here.

The old `C:\1Project\cyd-ps4` folder contains only leftover copied Git references.
Windows blocked moving those references; automatic approval review blocked their
removal. They are also preserved in this project's complete Git metadata.
There is no second firmware project in that old folder.

## Build status

The previous visual controller firmware compiled successfully on 2026-10-09 with
PlatformIO Core 6.1.19 and Click 8.1.8 (`work/build-display-verified.log`).
The current difficulty-menu and memory-game firmware also compiled successfully
on 2026-10-09 (`work/build-memory-game.log`). Nine compile-time behavior checks
passed against the actual game state machine. The firmware was not uploaded by Codex.
The generated `firmware.bin` is 814,256 bytes and fits the actual 1,048,576-byte
factory app partition. Its timestamp is newer than all firmware source changes.
The generated upload files are in `.pio/build/esp32dev`:
`firmware.bin`, `bootloader.bin`, and `partitions.bin`.

The user's monitor output confirms upload, an ESP32-D0WD-V3 with 4 MB flash,
and a successfully connected DualShock 4 (VID 054c, PID 09cc) on COM4. The latest
monitor output also confirms press/release events for all four face buttons,
the D-pad, L1/R1 and L2/R2. Controller pairing and input are verified on hardware.
Codex has not performed an upload itself. The new game needs an upload and
visual/controller confirmation on the CYD. The earlier pairing succeeded, but
the user's latest reconnection problem has not yet been verified on hardware.

`platformio.ini` selects the original ESP32 (`esp32dev`), ESP-IDF 5.4.2 and Arduino
3.2.1 through the official Bluepad32 template. The monitor output confirms
4 MB flash and Bluetooth Classic support. The display model still needs its
full board label/revision confirmed.

To keep the project in this folder, `build_dir` uses Windows' existing short name:

`C:/1Project/SOMERO~1/prj/GTAGAM~1/.pio/build`

That path refers to `.pio/build` INSIDE this same project. It avoids the explicit
ESP-IDF check rejecting spaces in the build-directory path. The successful build
also uses the path-remapping configuration described below.
If you rename/move the project or use another computer, update this short path.
Earlier generated build files are preserved under `work/previous-builds` so stale
paths are not reused. This is an archive of generated files, not another project.

## Build-tool compatibility

Use PlatformIO Core 6.1.19 for the pinned pioarduino 54.03.21 platform. Core 6.2.0
requires SCons 4.11.1, while this platform installs SCons 4.8.1; replacing the
running build engine caused the missing `SCons.Tool.FortranCommon` error.
The "Obsolete PIO Core" banner after downgrading is a warning, not a build error.

The bundled esptool 5.0 also requires Click below 8.2. The installed Click is now
8.1.8; newer versions failed while creating the bootloader binary. These working
tool versions are recorded in `requirements-build.txt`. To restore them from a
PowerShell terminal in this project folder:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install -r requirements-build.txt
```

This command adjusts the shared PlatformIO Python environment, not firmware source.

`sdkconfig.defaults` disables `CONFIG_COMPILER_HIDE_PATHS_MACROS`, and the current
`sdkconfig.esp32dev` has the same setting. This optional feature adds compiler
path-remapping arguments that PlatformIO splits at spaces in the project path.
Disabling it lets the project stay in this folder. Diagnostic file names may
contain their full paths; Bluetooth and controller handling are unchanged.

## Test in this order

1. Connect the CYD to your PC using a USB DATA cable. Leave the PS4 controller's
   USB cable unplugged; it connects directly to the CYD through Bluetooth.
2. In VS Code, choose File > Open Folder and select the project folder above.
3. Wait for PlatformIO initialization to finish. Do not start two builds at once.
4. Choose PlatformIO > Project Tasks > esp32dev > General > Build.
5. Continue ONLY if the build ends in SUCCESS. If it fails, copy the first error
   and the lines immediately around it. Do not upload a failed build.
6. Choose Upload. If several serial devices exist, select the CYD's actual COM
   port in PlatformIO. The port number is specific to your computer.
7. After upload succeeds, open Monitor at 115200 baud. Press the CYD RESET button
   if you missed the startup output.
8. Hold SHARE on the unplugged PS4 controller, then also hold PS until the light
   bar flashes. Wait for `GAMEPAD CONNECTED` and `PS4 CONTROLLER CONFIRMED`.
   On an already paired controller, try pressing PS first. The corrected sketch
   also prints `PS4 INPUT READY` after receiving a gamepad input report.
9. Release all controller buttons. Use the D-pad to select Easy or Hard, then X
   to start. Watch the sequence and repeat it when the screen says YOUR TURN.

Example input-test output:

```text
PS4 CONTROLLER CONFIRMED. ...
[0] X PRESSED
[0] X RELEASED
[0] O PRESSED
[0] O RELEASED
[0] R2 PRESSED
[0] R2 RELEASED
```

Button press/release events still appear in the monitor. Holding a button produces
one press event, then one release event when released. In the game, release between
presses, including repeated identical symbols. Presses during playback are ignored.

After button tests pass, power-cycle the CYD and press PS on the controller to
check reconnection. The test retains pairing keys across restarts.

## On-screen controls

The firmware initializes the CYD's backlight and a 320 x 240 landscape screen.
It shows pairing instructions while disconnected, then the difficulty menu. The
controller must send an input report and all buttons must be released before X
can start. The game works without keeping Serial Monitor open. Touch is not enabled.

1. D-pad Left/Up selects Easy; Right/Down selects Hard. X starts.
2. GET READY gives a short pause, then WATCH AND REMEMBER lights one symbol at a
   time. A dark gap separates each symbol, including consecutive identical ones.
3. YOUR TURN hides the sequence. Press the matching controller buttons in order,
   releasing between presses. The screen shows input progress and lights your presses.
4. ROUND CLEAR: X continues with the same sequence plus one new button.
5. A wrong button, simultaneous game-button press, or Hard timeout shows TRY AGAIN.
   X starts a new game in the selected difficulty.
6. Completing a 12-button sequence shows YOU WIN. X starts a new game.
7. OPTIONS returns to the difficulty menu at any stage. Disconnecting also returns
   to the menu. A second connected controller cannot alter the first player's game.

| Mode | Sequence buttons | First round | Flash / gap | Input deadline |
| --- | --- | --- | --- | --- |
| Easy | X, O, Square, Triangle | 2 symbols | 750 / 250 ms | None |
| Hard | Face buttons plus L1, R1, L2, R2 | 4 symbols | 350 / 180 ms | 5 seconds per button |

The game logic is in `main/memory_game.h`. Behavior tests in
`tests/memory_game_test.cpp` compile against that same implementation and cover
correct order, held buttons, repeated symbols, playback presses, wrong/chord input,
difficulty/timeouts, disconnects, round limits and clock wraparound. They are not
included in the flashed firmware.

The original ESP32-2432S028 uses an ILI9341 display. A common two-USB revision uses
ST7789. Both drivers are included. The default is ST7789 with inversion off,
matching the user's existing password-manager configuration. If the screen stays
blank, hold the physical BOOT button for one second
AFTER the firmware is running, then release it. This switches drivers and saves
the choice for the next restart. Pressing PS on the controller reconnects it.

Display pins: MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, backlight 21.
The display uses a small DMA buffer and redraws only changed areas after startup,
leaving memory for Bluetooth and the memory-combo game. Touch is not used.

Screen source is `main/cyd_display.cpp`; the LCD dependency is pinned in
`main/idf_component.yml`, and `main/CMakeLists.txt` includes the new source.

## Older Arduino projects

An older project using just `platform = espressif32` can select the installed
pioarduino platform because both platforms have the same package name. The
password-manager project has been pinned to `platformio/espressif32@6.10.0`
and uses `.pio/packages` inside its own folder for its Arduino 2.x packages.
Its configuration uses that directory's existing Windows short name to avoid
the GCC 8 compiler's path-length limit; the package files were not moved.
This avoids using the game's Arduino/ESP-IDF package versions. Its original
configuration is preserved in `.pio/config-backup/platformio.ini.before-compat-fix`
inside the password-manager folder. Password-manager source was not changed.
Its repaired firmware build passed on 2026-10-09; the log is
`work/build-password-manager-verified.log` in the game folder. It was not uploaded.

If no COM port appears, try another USB data cable/socket and check Windows
Device Manager for the board's USB device. If upload stops at Connecting, hold
BOOT until upload starts, then release it. Close other serial monitors before upload.

## If PS does not reconnect

No PC pairing utility or USB controller connection is needed for Bluepad32.
The CYD stays powered over USB; the controller connects directly over Bluetooth.

1. If the password-manager firmware was uploaded during screen testing, upload
   this game's firmware again first. The monitor must print `CYD MEMORY COMBO`.
   The password-manager firmware does not host a PS4 controller.
2. Unplug the controller USB cable. Temporarily turn off Bluetooth on the PC,
   phone or console that previously used the controller so another host cannot
   reconnect to it. Leave the CYD powered.
3. Hold SHARE, then also hold PS until the controller's light bar flashes.
   Wait for `PS4 CONTROLLER CONFIRMED`, then `PS4 INPUT READY`. The screen should
   show the difficulty menu as ready for selection. Pressing PS alone may fail
   after switching hosts or erasing pairing data; SHARE + PS performs pairing.
4. The user reports no light during SHARE + PS while unplugged, but a yellow
   light and Windows USB detection when plugged into the PC. The controller
   receives USB power; wireless power is not yet confirmed. Charge it from a
   powered USB port with a known-working cable,
   then unplug it and retry wireless pairing. If it still has no light, disconnect
   USB and hold the tiny reset button on the controller's back for at least five
   seconds. Charge/retry again. If it neither lights nor appears as a USB controller
   with another cable/port, investigate its charging port, battery or controller
   hardware; changing the ESP32 Bluetooth keys cannot power it on. If it continues
   working over USB but remains dark immediately after unplugging even after
   charging/resetting, the battery may not be holding a charge.
5. If the light flashes but it still will not pair, use the monitor's interactive
   console. `scan_and_autoconnect` reports whether discovery is enabled;
   `scan_and_autoconnect 1` enables it. The sketch already enables it at startup
   and after a disconnect. Keep saved bonds during normal restarts.
6. For persistent failed pairing only, enter `del_bluetooth_keys`, then press the
   CYD RESET button and repeat SHARE + PS. This deletes the CYD's saved controller
   bonds; it does not erase game code or the saved display-driver choice.
7. If it still fails, capture the monitor output from CYD reset through the entire
   SHARE + PS attempt. Connection and input-report errors need that log to diagnose.

The interactive console supports `help`. The sketch uses Bluepad32 `Console`,
avoiding a conflict with Arduino `Serial`.

The user's first monitor log also contained one `DS4: Unexpected report type and
len` message. If that keeps repeating after uploading the callback fix and no
button events appear, keep the new monitor output for input-report diagnosis.
Bluetooth connection alone does not confirm that button reports are decoded.

References: [Bluepad32 PlatformIO setup](https://bluepad32.readthedocs.io/en/latest/plat_arduino/)
and [supported controllers and pairing](https://bluepad32.readthedocs.io/en/latest/supported_gamepads/).
Controller reset: [Sony's DualShock 4 troubleshooting](https://www.playstation.com/en-us/support/hardware/troubleshoot-dualshock-4/).
Screen wiring: [CYD pins](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md)
and [two-USB display configuration](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/DisplayConfig/CYD2USB/User_Setup.h).
