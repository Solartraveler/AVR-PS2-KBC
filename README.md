PS2KBD for Atmel ATmega8
==============

Original description:

This is a simple PS/2 to Ascii converter I wrote for my 6502 based homebrew computer.

Pin 4 goes high to signal the host that a new character is ready to be read.
Pin 9 is PS/2 CLK
Pin 8 is PS/2 DATA
ASCII Character is written to Port A 


Modifications done in this version:

AVR changed to Atmega8
Pin 2 on port D is PS/2 CLK
Pin 3 on port D is PS/2 DATA
Pin 0 on port B signals new data (LED)
ASCII character is written as hex on the UART with 19200 baud.


