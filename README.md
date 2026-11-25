Rave It Out engine
==================

This is the engine for **Rave It Out**, with a native Apple Silicon port. It's based
on StepAMWorks (below), a fork of StepMania. The Rave It Out theme and noteskins are
included. No songs are included; put your own in the `Songs` folder.

## Build it yourself on a Mac (Apple Silicon)

You don't need any programming experience for this. It takes about 15-30 minutes,
and most of that is waiting.

**You need:** a Mac with an M1 chip or newer, about 3 GB of free space, and an
internet connection. Homebrew is *not* needed.

1. **Open Terminal.** Press `Command + Space`, type `Terminal`, and press Return.

2. **Install Apple's Command Line Tools** (skip this if you already have them).
   Copy this line into Terminal and press Return:

   ```bash
   xcode-select --install
   ```

   Click **Install** in the window that pops up and wait for it to finish. If it says
   the tools are already installed, that's fine; go to the next step.

3. **Download the code.** Copy this into Terminal and press Return:

   ```bash
   cd ~/Desktop && git clone --depth 1 --branch arm64-macos https://github.com/raveitoutofficial/raveitout-engine.git
   ```

   This makes a `raveitout-engine` folder on your Desktop.

4. **Build it.** Copy this into Terminal and press Return:

   ```bash
   cd ~/Desktop/raveitout-engine && ./build-mac.sh
   ```

   The script downloads what it needs (CMake, FFmpeg's source and libusb, each from
   its official site, with checksums verified) and compiles everything. Leave
   Terminal open until it says **Done!** If something goes wrong, it tells you what
   happened and where the log file is.

5. **Play.** Open the `raveitout-engine` folder on your Desktop and double-click
   **StepMania.app**. Put your songs in the `Songs` folder in that same folder.
   Your scores and settings are saved in the `Save` folder there too.

   If macOS asks for **Input Monitoring** permission, allow it. Keyboards and dance
   pads need it.

**To update later:** run `cd ~/Desktop/raveitout-engine && git pull && ./build-mac.sh`.
Updates are much quicker than the first build.

---

The original StepAMWorks readme follows.

StepAMWorks
=========

This is a fork of StepMania with various features.

## Features
* OptionsList implements more features that the regular options menu supports.
* ~~Update checker~~ Eventually
* ~~Windows PIUIO ported from OpenITG (thx to pk for the libUSB shit)~~ It doesn't work
* ~~BedrockSolid's PIUIO2Key for windows lights which may or may not work, I don't know, I don't have a PIUIO~~ It's not even in the CMakeLists right now
* PacDrive lights for both Windows and Linux.
* ^ Including the correct mappings for pump mode
* Barely functional co-op x4 that happens to be very buggy
* Support StepF2's note visibility and fake attributes.
* Bonus note for modcharts/missions that does not affect life (Check the wiki)
* All level, all difficulty sort
* Ability to show SortOrders next to folders if you are using the built in group select. Check MusicWheel metrics for more information.
* Hide charts with #SELECTABLE:EASY when not in Easy Mode.
* ~~Card Reader support~~ Need to redo, doesn't flush the buffer correctly right now and only works on Linux
* Windows XP support, since this is based on chrispable's "starworlds" StepMania fork.

## Compiling

Linux users, do this because it has libUSB: `apt-get install libusb-dev libusb-1.0.0 libusb-0.1-4`

Windows users, you need to use Visual Studio 2013 build tools if you want Windows XP support.

Then follow the rest like normal: https://github.com/stepmania/stepmania/wiki/Compiling-StepMania

## Linux PacDrive
In preferences.ini: `LightsDriver=Linux_PacDrive`

Check product ID with lsusb. Replace idProduct with the Product ID of your PacDrive and the OWNER="raveitout" with the name of your linux account. Then run this command.
`sudo echo SUBSYSTEMS=="usb", ATTRS{idVendor}=="d209", ATTRS{idProduct}=="1500", SYMLINK+="pacdrive", OWNER="raveitout" > /etc/udev/rules.d/90-pacdrive.rules`

## Debugging Lights
In preferences.ini: `DebugLights=1`, `LightsDriver=Broadcast`.

Use the RIO theme to show the lights debug.

## Resources

Check the wiki.

## Licensing, legal stuff

StepAMW is GPLv3.

For specific information/legalese:
* Arduino card reader code written by Alex123123, used with permission.
* The original MIT licensed code for StepAMW, if you would like to use it, is before this commit https://github.com/RhythmLunatic/stepmania/commit/476ecaed98b47ba4c9b12ee22c6f0d4de0434af3
* The wiki is [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/).
* The [MAD library](http://www.underbit.com/products/mad/) and [FFmpeg codecs](https://www.ffmpeg.org/) when built with our code use the [GPL license](http://www.gnu.org).

