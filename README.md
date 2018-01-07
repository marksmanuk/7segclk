# 7SegClk - 7 Segment LED Clock

This is a small program for displaying the current time or date on a 7 segment LED display driven by a Maxim MAX7219 from the Raspberry Pi.

The MAX7219 is a serially interfaced 8 digit LED display driver IC used in several Raspberry Pi add-on boards.  This codes was developed and tested using the ZeroSeg kit from PiHut which uses a MAX7219 connected to the SPI interface and provides two push buttons connected to GPIO inputs.

The code is written in C and can be easily modified to display other information as required.

I am using this program to display the current system time on a Pi based 60 kHz MSF clock transmitter.

## Getting Started

Download the latest release of this program and copy the files to your Raspberry Pi.  The code was developed and tested using Raspbian Stretch Lite on a Raspberry Pi Zero.

To compile, copy the Makefile and 7segclk.c to the Pi.  You will need gcc installed.

### Compiling

```
$ make
```

### Usage

To start the program, enter:

```
$ sudo ./7segclk
```
The program needs to run as root in order to memory map the GPIO address map to read the optional push buttons.

For help enter:

```
Usage: 7segclk [options]
        -i <0-15> Display intensity
        -c <0|1> Select clock display option
        -d <0|1> Select date display option
        -v Verbose
```

The default display modes are:

...
-c 0   High resolution time: hh.mm.ss.ms
-c 1   Normal resolution time: hh.mm.ss
-d 0   Date in format: yyyy.mm.dd (ISO style)
-d 1   Date in format: dd-mm-yy (UK style)  
...

The default is to display the time in high resolution format.
If the push buttons are fitted, they are used to increase or decrease the display intensity during run time.

### Accuracy

The program displays the current system date and time.  Ideally the Pi will be NTP synced to an upstream time server.  Check your selected timezone and change accordingly in Raspbian if needed.

I am running the NTP replacement daemon chrony (chronyd) synced to a local time server.

...
$ chronyc tracking
Reference ID    : 3F82F8A4 (time.xxxx.xx.xx)
Stratum         : 2
Ref time (UTC)  : Sun Jan 07 16:56:59 2018
System time     : 0.000000017 seconds slow of NTP time
Last offset     : +0.002727369 seconds
RMS offset      : 0.000904024 seconds
Frequency       : 10.184 ppm fast
Residual freq   : +0.064 ppm
Skew            : 0.741 ppm
Root delay      : 0.063919 seconds
Root dispersion : 0.029787 seconds
Update interval : 5178.6 seconds
Leap status     : Normal
...

The MAX7219 can be driven pretty hard and can easily update at 1/100th second.

### Prerequisites

* Raspberry Pi (tested with Pi Zero W)
* gcc

### Technical

The code assumes the following IO pins are used for the display:

...
DIN    Data In      Pin 19    GPIO10 (MOSI)
CS     Chip Select  Pin 24    GPIO8 (PI CE0)
CLK    Clock        Pin 23    GPIO11 (SPI CLK)

SW1    Left Button  Pin 11    GPIO17
SW2    Right Button Pin 37    GPIO26
...

## Authors

* **Mark Street** [marksmanuk](https://github.com/marksmanuk)

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details

