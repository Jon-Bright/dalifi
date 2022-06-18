# DaliFi
ESP8266-based PCB and code for WiFi communication with DALI-based devices. DALI is standardized as IEC 62386-102.

This project consists of three parts:

* hardware: Design for a fairly simple through-hole PCB capable of powering a bus, sending and receiving messages to/from DALI-compliant lamps. Design done using EAGLE, Gerber files included.
* library: An Arduino library for communicating with DALI-compliant lamps.
* example: An example Arduino application using the library and providing a WiFi-based interface to control attached lamps.

Each of these is covered in more detail below.

## Hardware

KNOWN BUG: the silkscreen for T2 is incorrect, at least for the 2N2222s I have.

The hardware can be broadly separated into seven parts:

* Mains voltage to 12v conversion.
  * This is done by a self-contained RAC05-12SK unit.
  * The resulting 12v is used to (optionally, see below) power the bus to the lamps, as well as the board itself.
* 12v to 3.3v conversion.
  * Another self-contained unit, RNM-123.3S.
  * The 3.3v is used to power the ESP8266.
* Bus power: this takes the 12v from above and limits its current as well as making it short-tolerant.
  * DALI communication involves frequent shorts of the bus (it's how "low" is communicated).
  * As more power is drawn on the bus (including when it's shorted), the voltage across R2 increases.
  * When the power drawn reaches 144mA, that's a voltage across R2 of 1.44V - and that's equivalent to the forward voltage of the two diodes. Current begins to flow through them.
  * The current flow across the two diodes means the transistor gradually turns off.
  * The current through the diodes is limited by R1, to about 12mA.
  * There are two jumpers after all of this. If something else is powering the bus, disconnect the jumpers and this circuit won't.
* DALI polarity protection.
  * If we're powering the bus, this isn't needed - we know which polarity is in use. But if we're not, then the bus can be either polarity. This rectifier deals with this and gives us plus and minus.
* DALI input.
  * When the DALI+ voltage is high enough (~5.6V) above DALI-, the Zener diode (D3) starts conducting.
  * This causes the LED in optoisolator OK1 to light.
  * R3 limits current across D3 (and through OK1).
  * When the LED in OK1 is active, DALI_I (which is otherwise pulled high by R5) is pulled low by OK1.
* DALI output.
  * When DALI_O is high, the LED in OK2 is active.  (R7 limits current through OK2 and current sourced from the ESP8266.)
  * This in turn causes current to flow to the base of T2 (via current-limiter R4). T2's base is otherwise pulled low by R6.
  * This causes T2 to short DALI+ and DALI-.  (A 2N2222 is good for 600mA continuous current and the DALI bus should be at most 250mA.)
* The ESP8266.
  * We're expecting a board with USB connector and so forth, as opposed to a blank ESP8266.

I am _not_ a hardware engineer. People who are will probably take one look at the design and go cry into pillows. Improvements are very welcome.

## Library

The library:

* Includes metadata for all opcodes (and "opcode addresses" like DAPC, DTR0) in the standard.
* Only implements functions to send a limited subset of those opcodes.
* Implementation of additional opcodes should be trivial.
* Includes functions for assigning short addresses to lamps.
* Handles all aspects of encoding and decoding the Manchester encoding used by devices.
* Is designed to work with the PCB above
  * Any PCB featuring an ESP8266 with two pins assigned to input from and output to DALI-compliant lamps should work, though.
  * If using a different PCB with differently-performing hardware, the timing definitions for half-bits at the top of dali.cpp might need tweaking.

## Example

The example Arduino application uses the library.  It features:

* Saving of WiFi connection using the ESP8266's (fake) EEPROM functionality.
* An Access Point function when no WiFi connection info is saved, to collect and save the information.
* A simple text-based interface for controlling the lamps.
* Commands for controlling lamps and querying their status.

My intention is to add MQTT support in addition to (or in place of) the simple text-based interface.

## Testing

I've tested this with three DALI-compliant lamps in my possession (two from the same manufacturer). It works fine with all of them. I've had it in operation with two of those lamps for a total of ~5 years of runtime without problems. Nevertheless, see the disclaimer of all warranty below.

## Legal

DALI, the DALI Logo, DALI-2, the DALI-2 Logo, DiiA, the DiiA Logo, D4i, the D4i Logo, DALI+ and the DALI+ Logo are trademarks in various countries in the exclusive use of the Digital Illumination Interface Alliance. No claim is made to any of these marks, nor is it claimed that this work is compliant with standards issued by the Alliance.

Absolutely no warranty is given for any element of this work. If it breaks, you get to keep both pieces. If it breaks your lamp(s), same. See the warranty exclusion in LICENSE.
