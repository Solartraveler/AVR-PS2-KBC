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
#include <avr/io.h>
#include <avr/interrupt.h>
#include "ps2kbd.h"
#include <util/delay.h>
#include <avr/wdt.h>
#include <stdio.h>

#include "rs232.h"

//must have interrupt 0
#define PS2CLOCK PD2
#define PS2DATA PD3
#define PS2PORT PORTD
#define PS2DDR  DDRD
#define PS2PIN  PIND

#define STATUSLEDDDR DDRB
#define STATUSLEDPORT PORTB
#define STATUSLED PINB0

const uint8_t ps2_to_ascii[] = // Scancode > Ascii table.
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '`', 0, // 00-0F
  0, 0, 0, 0, 0, 'q', '1', 0, 0, 0, 'z', 's', 'a', 'w', '2', 0, //10-1F
  0, 'c', 'x', 'd', 'e', '4', '3', 0, 0, ' ', 'v', 'f', 't', 'r', '5', 0, //20-2F
  0, 'n', 'b', 'h', 'g', 'y', '6', 0, 0, 0, 'm', 'j', 'u', '7', '8', 0, //30-3F
  0, ',', 'k', 'i', 'o', '0', '9', 0, 0, '.', '/', 'l', ';', 'p', '-', 0, //40-4F
  0, 0, '\'', 0, '[', '=', 0, 0, 0, 0, 0, ']', 0, '\\', 0, 0, //50-5F
  0, 0, 0, 0, 0, 0, 0, 0, 0, '1', 0, '4', '7', 0, 0, 0, //60-6F
  '0', '.', '2', '5', '6', '8', 0, 0, 0, '+', '3', '-', '*', '9', 0, 0, //70-7F
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 //80-8F
};

const uint8_t ps2_to_ascii_shifted[] =
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '~', 0, // 00-0F
  0, 0, 0, 0, 0, 'Q', '!', 0, 0, 0, 'Z', 'S', 'A', 'W', '@', 0, //10-1F
  0, 'C', 'X', 'D', 'E', '$', '#', 0, 0, ' ', 'V', 'F', 'T', 'R', '%', 0, //20-2F
  0, 'N', 'B', 'H', 'G', 'Y', '^', 0, 0, 0, 'M', 'J', 'U', '&', '*', 0, //30-3F
  0, '<', 'K', 'I', 'O', ')', '(', 0, 0, '>', '?', 'L', ':', 'P', '_', 0, //40-4F
  0, 0, '"', 0, '{', '+', 0, 0, 0, 0, 0, '}', 0, '|', 0, 0, //50-5F
  0, 0, 0, 0, 0, 0, 0, 0, 0, '1', 0, '4', '7', 0, 0, 0, //60-6F
  '0', '.', '2', '5', '6', '8', 0, 0, 0, '+', '3', '-', '*', '9', 0, 0, //70-7F
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 //80-8F
};

// Volatile, declared here because they're used in and out of the ISR
volatile uint8_t rcv_byte = 0;
volatile uint8_t rcv_bitcount = 0;
volatile uint8_t send_bitcount = 0;
volatile uint8_t scancode = 0;
volatile uint8_t ssp = 0; // 0 = Start/ 1 = stop/ 2 = parity
volatile uint8_t send_parity = 0;
volatile uint8_t send_byte = 0;
volatile uint8_t parity_errors = 0; // Currently unused but will provide error info to host computer
volatile uint8_t framing_errors = 0;
volatile enum bufstate buffer = EMPTY;
volatile enum ps2state mode = KEY;
volatile enum rxtxstate sr = RX;

int calc_parity(unsigned parity_x)
{
  // Calculate Odd-Parity of byte needed to send PS/2 Packet
  unsigned parity_y;
  parity_y = parity_x ^ (parity_x >> 1);
  parity_y = parity_y ^ (parity_y >> 2);
  parity_y = parity_y ^ (parity_y >> 4);
  return parity_y & 1;
}

void framing_error(uint8_t num)
{
  // Deal with PS/2 Protocol Framing errors. delay for the rest of the packet and clear interrupts generated during the delay.
  framing_errors++;
  GICR &= ~(1 << INT0);
  _delay_ms(8);
  GIFR |= (1 << INTF0); // Clear Interrupt flag
  GICR |= (1 << INT0);
}

void sendps2(uint8_t data)
{
/*  Send a PS/2 Packet.
  Begin the request by making both inputs outputs, drag clock low for at least 100us then take data low and release clock.
  the device will soon after start clocking in the data so make clk an input again and pay attention to the interrupt.
  The device will clock in 1 start bit, 8 data bits, 1 parity bit then 1 stop bit. It will then ack by taking data low on the 12th clk (though this is currently ignored) and then it will respond with an 0xFA ACK */
  uint8_t send_tries = 3;
  scancode = 0;
  do
  {
    send_byte = data;
    send_parity = calc_parity(send_byte);
    GICR &= ~(1 << INT0); // Disable interrupt for CLK
    PS2PORT &= ~(1 << PS2DATA); // Set data Low
    PS2PORT &= ~(1 << PS2CLOCK); // Set Clock low
    PS2DDR |= (1 << PS2CLOCK); // CLK low
    _delay_us(150);
    PS2DDR |= (1 << PS2DATA); // DATA low
    PS2DDR &= ~(1 << PS2CLOCK); // Release clock and set it as an input again, clear interrupt flags and re-enable the interrupts
    GIFR |= (1 << INTF0);
    GICR |= (1 << INT0);
    sr = TX;
    while (sr == TX) {} // All the work for sending the data is handled inside the interrupt
    PS2DDR &= ~(1 << PS2CLOCK | 1 << PS2DATA); // Clock and Data set back to input
    buffer = EMPTY;
    while (buffer == EMPTY) {} // Wait for ACK packet before proceeding
    buffer = EMPTY;
    send_tries--;
  } while ((send_tries) && (scancode != 0xFA)); // If the response is not an ack, resend up to 3 times.
  _delay_us(150);
}

int getresponse(void) {
    mode = COMMAND;
    while (buffer == EMPTY) {}
    mode = KEY;
    buffer = EMPTY;
    return scancode;
}

void resetKbd(void) {
  sendps2(0xff); // reset kbd
  uint8_t resp = getresponse();
  if (resp != 0xAA) {
    while (1) {} // Trigger WDT Reset
  }
  sendps2(0xf0); // Set Codeset
  sendps2(0x02); // Codeset 2
}

void resetHost(void) {
    PS2DDR |= (1 << PS2CLOCK);
    PS2PORT &= ~(1 << PS2CLOCK);
    for (;;) {} // Reset KBC using WDT
}

void parity_error(void)
{
  parity_errors++;
  sendps2(0xFE); // Inform the KBD of the Parity error and request a resend.
}

ISR (INT0_vect)
{
  if (sr == TX) { //Send bytes to device.
    if (send_bitcount >=0 && send_bitcount <=7) // Data Byte
    {
      if ((send_byte >> send_bitcount) & 1) {
        PS2DDR &= ~(1 << PS2DATA); // DATA High
      }
      else
      {
        PS2DDR |= (1 << PS2DATA); // DATA Low
      }
    }
    else if (send_bitcount == 8) // Parity Bit
    {
      if (send_parity)
      {
        PS2DDR |= (1 << PS2DATA); // DATA Low
      }
      else
      {
        PS2DDR &= ~(1 << PS2DATA); // DATA High
      }
    }
    else if (send_bitcount == 9) // Stop Bit
    {
      PS2DDR &= ~(1 << PS2DATA); // DATA High
    }
    if (send_bitcount < 10)
    {
      send_bitcount++;
    }
    else
    {
      send_bitcount = 0;
      sr = RX;
    }
  }

  else { // Receive from device
  uint8_t result = 0;

    if (PS2PIN & (1 << PS2DATA))
    {
      result = 1;
    }
    else {
      result = 0;
    }

    if (rcv_bitcount <=9)
    {
      if (rcv_bitcount >=1 && rcv_bitcount <= 8)
      {
        rcv_byte |= (result << (rcv_bitcount - 1)); //Scancode Byte
      }
      else if (rcv_bitcount == 0)
      {
        ssp = result; // Start Bit
      }
      else if (rcv_bitcount == 9)
      {
        ssp |= (result << 2); // Parity Bit
      }
      rcv_bitcount++;
    }
    else if (rcv_bitcount == 10)
    {
      ssp |= (result << 1); // Stop Bit
      if ((ssp & 0x2) != 0x02) // Check start and stop bits.
      {
        framing_error(ssp);
      }
      else if (calc_parity(rcv_byte) == (ssp >> 2))
      {
        parity_error();
        buffer = EMPTY;
      }
      else
      {
        scancode = rcv_byte;
        buffer = FULL;
      }
      rcv_bitcount = 0;
      rcv_byte = 0;
      result = 0;
    }

  }

}

int main (void) {
  volatile uint8_t kb_register = 0;
  volatile uint8_t kb_leds = 0;
  volatile char ret_char = 0;

  MCUCR |= (1 << ISC01); // Interrupt on Falling Edge, force disable pullups
  SFIOR |= (1 << PUD);
  PS2DDR &= ~(1 << PS2CLOCK | 1 << PS2DATA); // PINB6 = PS/2 Clock, PINB5 = PS/2 Data both set as input
  STATUSLEDDDR |= (1 << STATUSLED);
  GICR |= (1 << INT0); // Enable Interrupt on PINB2 aka INT0

  uart_init();
  printf("Hello\r\n");

  wdt_enable(WDTO_500MS);
  sei();
  resetKbd();

  while (1) {
    wdt_reset();
    if ((kb_register & (1 << KB_L_CTRL)) && (kb_register & (1 << KB_L_ALT)) && (kb_register & (1 << KB_R_ALT))) {
      resetHost();
      while (1) {
        // Reset KBC using the WDT
      }
    }
    if ((buffer == FULL) && ((mode == KEY) || (mode == EXTKEY)))
    {
      GICR &= ~(1 << INT0); // Disable interrupt for CLK
      PS2PORT &= ~(1 << PS2CLOCK); // Bring Clock low, inhibit keyboard until done processing event
      PS2DDR |= (1 << PS2CLOCK); // CLK now an outout
      if (mode == EXTKEY) {
        if (kb_register & (1 << KB_KUP)) //This is a keyup event
        {
          switch(scancode)
          {
            case 0x14:
              kb_register &= ~(1 << KB_R_CTRL);
              break;
            case 0x11:
              kb_register &= ~(1 << KB_R_ALT);
              break;
            default:
              break;
          }
          kb_register &= ~(1 << KB_KUP);
          mode = KEY;
        }
        else {
          switch(scancode)
          {
            case 0xF0: //Key up
              kb_register |= (1 << KB_KUP);
              mode = EXTKEY;
              break;
            case 0x14: //ctrl
              kb_register |= (1 << KB_R_CTRL);
              mode = KEY;
              break;
            case 0x11: //alt
              kb_register |= (1 << KB_R_ALT);
              mode = KEY;
              break;
            default:
              mode = KEY;
              break;
          }
        }
      }
      else if (mode == KEY)
      {
        if (kb_register & (1 << KB_KUP)) //This is a keyup event
        {
          switch(scancode)
          {
            case 0x12: // Left Shift
              kb_register &= ~(1 << KB_L_SHIFT);
              break;
            case 0x59: // Right Shift
              kb_register &= ~(1 << KB_R_SHIFT);
              break;
            case 0x14:
              kb_register &= ~(1 << KB_L_CTRL);
              break;
            case 0x11:
              kb_register &= ~(1 << KB_L_ALT);
              break;
            default:
              break;
          }
          kb_register &= ~(1 << KB_KUP);
          mode = KEY;
        }
        else {
          switch(scancode)
          {
            case 0xF0: //Key up
              kb_register |= (1 << KB_KUP);
              break;
            case 0xE0: //Extended key sequence
              mode = EXTKEY;
              break;
            case 0x12: // Left Shift
              kb_register |= (1 << KB_L_SHIFT);
              break;
            case 0x59: // Right Shift
              kb_register |= (1 << KB_R_SHIFT);
              break;
            case 0x66: //backspace
              ret_char = 0x7F;
              break;
            case 0x5A: //enter
              ret_char = 0x0D;
              break;
            case 0x0D: //tab
              ret_char = 0x09;
              break;
            case 0x14: //ctrl
              kb_register |= (1 << KB_L_CTRL);
              break;
            case 0x11: //alt
              kb_register |= (1 << KB_L_ALT);
              break;
            case 0x76: //esc
              ret_char = 0x1B;
              break;
            case 0x58: //capslock
              kb_leds ^= (1 << KB_CAPSLK);
              sendps2(0xed);
              sendps2(kb_leds & 0x07); // Set KBD Lights
              break;
            case 0x77: //numlock
              kb_leds ^= (1 << KB_NUMLK);
              sendps2(0xed);
              sendps2(kb_leds & 0x07); // Set KBD Lights
              break;
            case 0x7E: //scrllock
              kb_leds ^= (1 << KB_SCRLK);
              sendps2(0xed);
              sendps2(kb_leds & 0x07); // Set KBD Lights
              break;
            default: // Fall through for Alphanumeric Characters
              if ((kb_register & (1 << KB_L_CTRL)) || (kb_register & (1 << KB_R_CTRL))) // ASCII Control Code
              {
                ret_char = ps2_to_ascii_shifted[scancode];
                if ((ret_char >=0x41) && (ret_char <= 0x5A)) //Make sure we don't read outside the valid range of codes
                {
                  ret_char ^= 0x40;
                }
                else
                {
                  ret_char = 0;
                }
              }
              else if ((kb_register & (1<< KB_L_SHIFT)) || (kb_register & (1<<KB_R_SHIFT))) {
                ret_char = ps2_to_ascii_shifted[scancode];
              }
              else if (kb_leds & (1 <<KB_CAPSLK)) {
                ret_char = ps2_to_ascii[scancode];
                if ((ret_char >= 0x61) && (ret_char <= 0x7A))
                {
                  ret_char ^= 0x20;
                }
              }
              else
              {
                ret_char = ps2_to_ascii[scancode];
              }
              break;
          }
          if (ret_char)
          {
            STATUSLEDPORT |= 1 << STATUSLED;
            _delay_us(10);
            STATUSLEDPORT &= ~(1 << STATUSLED);
            printf("Char %x\r\n", ret_char);
            ret_char = 0;
          }
        }
      }
      buffer = EMPTY;
      PS2DDR &= ~(1 << PS2CLOCK);
      GIFR |= (1 << INTF0);
      GICR |= (1 << INT0);
    }
  }
}
