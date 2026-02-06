# MakePython ESP32 Bluetooth Walkman

A MakePython ESP32-based offline Walkman (WAV player) that streams audio from an SD card to Bluetooth earphones using A2DP Source. Includes an SSD1306 OLED UI, physical button controls, a simple on-device file browser, and resume-from-last-position playback history stored on the SD card.

## Features

- **Bluetooth A2DP Source streaming** to a paired device (earphones/speaker)
- **Plays WAV files from SD card**
- **OLED UI (SSD1306 128×64)** with:
  - Connecting screen
  - Now playing screen (track, time, progress bar, status)
  - File browser screen (scroll + highlight)
- **Physical button controls**
  - Next / Previous
  - Play / Pause
  - Menu / Back (file browser)
- **Playback resume**
  - Saves last played file + byte position in `/playback.dat`
  - Auto-resumes on reconnect
  - Periodic autosave while playing

<img width="546" height="807" alt="image" src="https://github.com/user-attachments/assets/9afc44f4-77b7-44ac-ae1f-634d301bff0f" />

<img width="525" height="591" alt="image" src="https://github.com/user-attachments/assets/d9fb69fa-cf41-4c9e-861e-0486ce983d2a" />

<img width="468" height="610" alt="image" src="https://github.com/user-attachments/assets/ea6a44a7-b4cc-4d93-afd5-5b8085682d69" />


## Hardware

### Required
- **MakePython ESP32 Audio Player** https://www.makerfabs.com/eps32-audio-player.html 
- **MicroSD card**

### Buttons
This project uses 6 buttons:
- Vol Up
- Vol Down
- Mute / Menu
- Previous
- Pause / Select
- Next

> Note: In the current code, Vol Up / Vol Down pins are defined but not yet used for A2DP volume control. (Controls implemented: Next, Previous, Pause, Mute/Menu)

## Audio Format Requirements
This project assumes **16-bit PCM stereo WAV @ 44.1 kHz**.
The timing math in code uses:
- `176400 bytes/sec` (44,100 samples/sec × 2 channels × 2 bytes/sample)
If your files are not 44.1kHz/16-bit/stereo, playback speed/time display may be wrong or audio may fail.

## UI Screens & Controls

### 1) Connecting Screen
- Shows Bluetooth target name and animated status while connecting.

### 2) Playing Screen
- Displays BT status, track number, filename (trimmed), time, progress bar, and state (PLAYING/PAUSED/STOPPED).

Controls:
- **Next**: next track
- **Previous**: if >3 seconds into track, restart; else previous track
- **Pause**: toggle pause/resume (saves history on pause)
- **Menu (Mute button)**: open file browser

### 3) File Browser Screen
- Lists 5 visible items, highlights selected row, shows playing indicator.

## How Resume Works

- On connect, if `/playback.dat` exists:
  - Reads `file=<path>` and `pos=<bytePosition>`
  - Seeks to `dataStart + pos` (aligned to 4-byte frame boundary)
- While playing:
  - Saves every ~10 seconds
- On pause or Bluetooth disconnect:
  - Saves immediately
