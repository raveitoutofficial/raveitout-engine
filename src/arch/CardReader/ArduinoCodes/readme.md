# InuReader

Created by Alex123123, adapted by Accelerator for SM

This code essentially supports any Arduino since it's just sending the card data through serial.

Yes the code has keypad support, DO NOT attach a keypad since StepMania doesn't support it.

## Requirements

- Arduino. Pretty much any will work if they have the right pins. If you don't know what to buy then buy a Leonardo. If you only need MFRC522 then buy a Nano.
- MFRC522 Module, HSJ632TR Module, or LMRF 3060 module.

Note: MFRC522 only supports MIFARE, so only Nesica, Aime, Banapass will work. AM Pass and eAmusement pass will not work.

## Setup
### For MFRC522:
| Signal | MFRC522 Reader/<br>PCD Pin | Arduino Leonardo/<br>Micro Pin | Arduino Pro Micro Pin | Arduino Nano |
| ------ | ------------------------ | ---------------------------- | --------------------- | ------------ |
| RST/Reset | RST | RESET/ICSP-5 | RST | RST |
| SPI SS | SDA(SS) | A0 | A0 | A0 |
| SPI MOSI | MOSI | ICSP-4 | 16 | D11 |
| SPI MISO | MISO | ICSP-1 | 14 | D12 |
| SPI SCK | SCK | ICSP-3| 15 | D13 |

### For HSJ632TR and LMRF 3060
Connect the reader module's RX to TX(on Pro Micro) ,TX to RX (on Pro Micro).
Then connect USB cable.
Nothing else.

Then flash the arduino code for your reader, of course.

## Testing
### Linux/Unix:
Compile readerTest3.cpp. Edit source code to point to your serial port.

`g++ readerTest3.cpp -o readerTest3`

Run in terminal, card reader should print to console.

Alternatively you can run `od --endian=big -x < /dev/ttyUSB0` where ttyUSB0 is the correct port and it will print to terminal. Note that it sends 11 bytes at a time and the first four bytes should be ignored ("aaaa").

### Windows:
Coming soon...

## Configuration in StepMania

Card login is **off by default** and stays completely dormant (no thread, no
port opened) until you turn it on in two places:

1. **Engine:** in `Save/Preferences.ini`, set the serial port of each reader:

   ```
   CardReaderPortP1=/dev/cu.usbmodem14101
   CardReaderPortP2=
   CardReaderBaud=9600
   ```

   - macOS: `/dev/cu.usbmodem...` or `/dev/cu.usbserial...` (use `cu.`, not `tty.`)
   - Windows: `COM3` etc. (Device Manager > Ports)
   - Linux: `/dev/ttyACM0` or `/dev/ttyUSB0`

   P1 and P2 each get their own reader, so the game knows which side was
   tapped. Leave one empty for a single reader. If a port can't be opened,
   `Logs/log.txt` lists the serial ports it found; the game keeps retrying, so
   readers can be plugged in after it starts.

2. **Theme:** Options > SAVE TYPE > "USB and RFID".

Both are needed. With only one set, the game uses normal profiles.

### What the engine sends the theme

Each valid scan broadcasts `CardScanned` with
`{ Player = PlayerNumber, card = "DEADBEEF00000000" }`: the 8 ID bytes as 16
uppercase hex digits. Packets with a bad checksum are dropped, and the same
card held on the same reader is ignored for 3 seconds. `CARDREADER:IsEnabled()`
(optionally with a PlayerNumber) tells Lua whether any reader is configured.

### What the Rave It Out theme does with it

- On ScreenSelectProfile: a known card selects its profile; a new card creates
  a profile called "New Player" whose ID (folder in `Save/LocalProfiles/`) is
  the card ID.
- During the attract demo: a scan starts a session and goes to profile select.
- See `CardReaderAvailable()` in `Themes/RaveItOut/Scripts/06 MemcardHelpers.lua`.

### Testing without hardware

Any program that writes the 11-byte packet (`AA AA`, 8 ID bytes, then
`~(sum of the previous 10 bytes)`) to a serial port works. On macOS/Linux a
pseudo-terminal (Python `os.openpty()`) can stand in for the reader: point
`CardReaderPortP1` at the pty's path.
