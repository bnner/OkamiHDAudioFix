# OkamiHDAudioFix

A small Windows audio compatibility fix for **Ōkami HD**.

## What It Fixes

Ōkami HD can fail to produce audio on some Windows systems when the active playback device exposes a high sample rate or multichannel format, such as:

- 96 kHz
- 192 kHz
- 5.1 / 7.1 / 8-channel output
- virtual audio devices
- external audio devices using high-rate shared-mode formats

A common workaround is to manually change the Windows playback device to 48 kHz before launching the game.

**OkamiHDAudioFix removes the need to change the global Windows audio configuration.**

## How It Works

Ōkami HD uses Windows WASAPI through its audio middleware.

On affected systems, Windows may expose a mix format such as:

```text
8 channels
96000 Hz
32-bit IEEE float
```

The game can initialize against this kind of format but still fail to produce sound correctly.

OkamiHDAudioFix intercepts the WASAPI format used by the game and presents Ōkami HD with a known-working format:

```text
2 channels
48000 Hz
32-bit IEEE float
```

The Windows shared-mode audio engine is then allowed to resample and convert that stream to the actual playback device format.

This means the user's Windows audio settings can remain unchanged.

## Installation

1. Download or build the x64 release version of the fix.
2. Make sure the DLL is named `dinput8.dll`.
3. Copy `dinput8.dll` into the Ōkami HD installation directory, beside:

```text
okami.exe
main.dll
flower_kernel.dll
```

4. Launch Ōkami HD normally through Steam.

No Windows audio setting changes should be required.

## Uninstallation

Delete `dinput8.dll` from the Ōkami HD installation directory.

The game will then return to using the normal Windows DirectInput library.

## Logging

The fix creates a small diagnostic log named:

```text
OkamiHDAudioFix.log
```

in the game directory.

A normal startup should contain messages similar to:

```text
OkamiHDAudioFix loaded.
System dinput8.dll loaded.
Audio fix initialization started.
WASAPI hooks installed successfully.
Adjusted game audio format to stereo 48 kHz float.
```

The log is only intended for troubleshooting.

It does not contain continuous audio data.

## Technical Details

The game loads `DINPUT8.dll` through `flower_kernel.dll`.

OkamiHDAudioFix acts as a proxy for:

```text
DirectInput8Create
```

The call is forwarded to the real Windows `dinput8.dll`.

During startup, the fix creates a temporary `IAudioClient` instance so it can obtain the Windows WASAPI implementation addresses from the object's virtual function table.

MinHook is then used to intercept:

```text
IAudioClient::GetMixFormat
IAudioClient::Initialize
```

### IAudioClient::GetMixFormat

Windows normally returns the active playback endpoint's shared-mode mix format.

For example:

```text
8 channels
96000 Hz
32-bit IEEE float
```

If that format differs from the known-working Ōkami HD format, OkamiHDAudioFix changes the format presented to the game to:

```text
WAVE_FORMAT_EXTENSIBLE
2 channels
48000 Hz
32-bit IEEE float
Stereo channel mask
```

The physical Windows playback device itself is not reconfigured.

### IAudioClient::Initialize

Ōkami HD initializes its audio stream through WASAPI shared mode.

OkamiHDAudioFix enables:

```text
AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
```

when the game initializes the audio client.

This allows the Windows shared-mode audio engine to convert the game's 48 kHz stereo stream to the actual output device format.

The resulting audio path is effectively:

```text
Ōkami HD
    |
    | 2-channel / 48 kHz / 32-bit float
    v
Windows WASAPI shared-mode audio engine
    |
    | sample-rate and channel conversion as required
    v
Configured Windows playback device
```

## Tested Behavior

The fix was developed on a system where the active Windows playback endpoint reported:

```text
8 channels
96000 Hz
32-bit IEEE float
```

Without the fix, Ōkami HD produced no sound.

With OkamiHDAudioFix:

- the game produces sound normally
- Windows can remain configured at 96 kHz
- the playback device can be changed while the game is running
- audio continues working after device changes
- already-working 48 kHz configurations continue to function

## Building

### Requirements

- Windows
- Visual Studio
- Windows SDK
- x64 build target
- vcpkg
- MinHook

The project is built as a 64-bit DLL.

### vcpkg Manifest

The project uses a `vcpkg.json` manifest containing MinHook.

Example:

```json
{
  "dependencies": [
    "minhook"
  ]
}
```

### MinHook

MinHook should be linked statically.

Install the static x64 triplet with:

```cmd
vcpkg install --triplet x64-windows-static
```

The static library should be available under:

```text
vcpkg_installed\x64-windows-static\lib\minhook.x64.lib
```

The final release DLL should not depend on:

```text
minhook.x64.dll
```

### Verify DLL Exports

Open an x64 Visual Studio Developer Command Prompt and run:

```cmd
dumpbin /exports OkamiHDAudioFix.dll
```

The output should contain:

```text
DirectInput8Create
```

Example:

```text
ordinal hint RVA      name

      1    0          DirectInput8Create
```

### Verify DLL Dependencies

Run:

```cmd
dumpbin /dependents OkamiHDAudioFix.dll
```

The final DLL should not list:

```text
minhook.x64.dll
```

Normal Windows and Microsoft runtime dependencies are expected, such as:

```text
MSVCP140.dll
ole32.dll
KERNEL32.dll
VCRUNTIME140_1.dll
VCRUNTIME140.dll
api-ms-win-crt-*.dll
```

## Module Definition File

The project uses a module definition file named:

```text
OkamiHDAudioFix.def
```

with the following contents:

```def
LIBRARY dinput8

EXPORTS
    DirectInput8Create
```

This ensures the proxy DLL exposes the function expected by the game.

## Release Build

Build using:

```text
Configuration: Release
Platform: x64
```

The project output is:

```text
OkamiHDAudioFix.dll
```

For installation or distribution, make a copy and rename it to:

```text
dinput8.dll
```

The deployed DLL must be named `dinput8.dll` because that is the library the game loads.

## Suggested Project Structure

```text
OkamiHDAudioFix/
├── OkamiHDAudioFix/
│   ├── dllmain.cpp
│   ├── framework.h
│   ├── OkamiHDAudioFix.def
│   ├── OkamiHDAudioFix.vcxproj
│   ├── OkamiHDAudioFix.vcxproj.filters
│   ├── pch.cpp
│   ├── pch.h
│   ├── vcpkg.json
│   └── vcpkg_installed/
│
├── x64/
│   └── Release/
│       └── OkamiHDAudioFix.dll
│
├── OkamiHDAudioFix.slnx
├── README.md
└── LICENSE
```

## Release Package

A minimal release package only needs:

```text
OkamiHDAudioFix/
├── dinput8.dll
├── README.md
└── LICENSE
```

The following build files are not required for normal users:

```text
OkamiHDAudioFix.lib
OkamiHDAudioFix.exp
OkamiHDAudioFix.pdb
```

The `.pdb` file may optionally be included separately for debugging.

## Troubleshooting

### The Game Does Not Launch

Remove `dinput8.dll` from the game directory and confirm the game launches normally without the fix.

Then verify:

- the DLL was built for x64
- `DirectInput8Create` is exported
- `minhook.x64.dll` is not listed as a dependency

Check exports with:

```cmd
dumpbin /exports OkamiHDAudioFix.dll
```

Check dependencies with:

```cmd
dumpbin /dependents OkamiHDAudioFix.dll
```

### The Game Launches but There Is No Audio

Check:

```text
OkamiHDAudioFix.log
```

in the game directory.

A successful initialization should contain:

```text
OkamiHDAudioFix loaded.
System dinput8.dll loaded.
Audio fix initialization started.
WASAPI hooks installed successfully.
```

When the fix adjusts an incompatible audio format, the log should also contain:

```text
Adjusted game audio format to stereo 48 kHz float.
```

### No Log File Is Created

If `OkamiHDAudioFix.log` is never created, the proxy DLL may not be loading.

Verify that `dinput8.dll` is located directly beside `okami.exe` and that the DLL exports:

```text
DirectInput8Create
```

## Removing the Fix

OkamiHDAudioFix makes no permanent system changes.

To completely remove it, delete:

```text
dinput8.dll
OkamiHDAudioFix.log
```

from the Ōkami HD installation directory.

## License

This project is licensed under the MIT License.

See the `LICENSE` file for details.
