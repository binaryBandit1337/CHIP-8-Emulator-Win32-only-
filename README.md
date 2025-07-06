# CHIP-8 Emulator

This is a emulator running at  60fps for the interpreted programming language developed by Joseph Weisbecker in the 1970s. It's written in C++ using the Win32 API and no third party tools.

Further information about CHIP-8, the memory, registers etc. and an opcode table can you find here https://en.wikipedia.org/wiki/CHIP-8.

## Screenshot

![IBM Logo](screenshots/IBM_logo.png)

## Requirements

You need [Visual Studio](https://visualstudio.microsoft.com/de/downloads/) If you don't want to use the IDE you can just download the Buildtools for Visual Studio. Justs scroll down on the website and look for Tools for Visual Studio.

## Compile the Source Code

Save the project and open it in Visual Studio or compile it on the command line:
!!! You need to use the Native Tools Command Prompt for VS not the standard CMD !!!


```
$ cl main.cpp /link user32.lib gdi32.lib shell32.lib 
```

You can add other compiler flags if you want to.

## Running a game

To run a game you can drag and drop it onto the .exe in Windows or you can call it from the command line with 

```
$ chip8.exe /pathToRom
```

## 💡 Implementation Details

### Sound: Non-blocking Beep using Detached Thread

CHIP-8 originally had no complex audio output — it simply produced a short tone whenever the sound timer was active.

To replicate this, the emulator uses the Windows API function `Beep()` to play a simple tone. However, `Beep()` is **blocking**, meaning it would freeze the main thread during sound playback and disrupt rendering and input.

To prevent this, the sound is emitted in a separate, **detached thread**:

```cpp
std::thread beepThread(playSound);
beepThread.detach();
```

This ensures the emulator keeps running at a smooth 60 FPS and remains responsive while emitting tones — without needing external audio libraries.

### 🖼️ Rendering: Software-based Scaling with SetDIBitsToDevice

CHIP-8 games use a **64×32 monochrome resolution**, which is upscaled to a larger window (e.g. 640×320) for display.

To keep rendering fully in **software**, this emulator uses the WinAPI function `SetDIBitsToDevice()`:

```cpp
SetDIBitsToDevice(hdc, 0, 0, (width * upscalefactor), (height * upscalefactor), 0, 0, 0, (height * upscalefactor), upscaledgraphic10x, (BITMAPINFO*)&bmi, DIB_RGB_COLORS);
```

Instead of StretchDIBits() (which may use hardware acceleration), this approach ensures that every frame is rendered purely in software.

## License

This project is licensed under the MIT. The file LICENSE includes the full license text.
