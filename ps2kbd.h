#pragma once
/*
PS2KBC, a PS2 Controler implemented on the Atmel ATTINY861.
Copyright (C) 2015 Matt Harlum

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#define KB_KUP     0
#define KB_L_SHIFT 1
#define KB_L_CTRL  2
#define KB_L_ALT   3
#define KB_R_SHIFT 4
#define KB_R_CTRL  5
#define KB_R_ALT   6

#define KB_SCRLK 0
#define KB_NUMLK 1
#define KB_CAPSLK 2


#ifndef F_CPU
#define F_CPU 8000000UL
#endif

enum ps2state {
    KEY,
    EXTKEY,
    PAUSE,
    COMMAND,
};

enum bufstate {
    FULL,
    EMPTY
};

enum rxtxstate {
    TX,
    RX
};

