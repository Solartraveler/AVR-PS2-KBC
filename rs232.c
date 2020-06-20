
#include <stdio.h>
#include <inttypes.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include "ps2kbd.h"

#define BAUDRATE 19200
#define UARTNUMBER (F_CPU/(BAUDRATE*16l)-1)

#define UNUSED(x) (void)(x)

/*If the RX pin is not connected, there is sometimes garbage in the buffer,
  which is in some cases detected as valid input. So by requireing a special
  char fist, this disables these false positives.
*/

#ifndef NODEBUGOUTPUT

uint8_t g_inputEnabled = 0;

int uart_put(char var, FILE *stream) {
	UNUSED(stream);
#ifdef __AVR_ATmega8__
	while (!(UCSRA & (1<<UDRE)));
	UDR = var;
#else
	while (!(UCSR0A & (1<<UDRE0)));
	UDR0 = var;
#endif
	return 0;
}

void uart_init(void) {
	//pullup prevents detecting noise as data when pin is not connected
	PORTD |= (1<<PD0); //RX pullup
#ifdef __AVR_ATmega8__
	UBRRL = UARTNUMBER; //set speed
	UCSRC = (1<<URSEL) | (1<<UCSZ1) | (1<<UCSZ0); //set 8Bit mode
	UCSRB = 1<<RXEN | 1<<TXEN;	//TX enabled, RX enabled
#else
	UBRR0L = UARTNUMBER; //set speed
	UCSR0C = (1<<UCSZ01) | (1<<UCSZ00); //set 8Bit mode
	UCSR0B = 1<<RXEN0 | 1<<TXEN0;	//TX enabled, RX enabled
#endif
	static FILE mystdout = FDEV_SETUP_STREAM(uart_put, NULL, _FDEV_SETUP_WRITE);
	stdout = &mystdout;
}

uint8_t rs232_key(void) {

#ifdef __AVR_ATmega8__
	if (UCSRA & (1<<RXC))
#else
	if (UCSR0A & (1<<RXC0))
#endif
		{
#ifdef __AVR_ATmega8__
		uint8_t d = UDR;
#else
		uint8_t d = UDR0;
#endif
		if (g_inputEnabled) {
			if ((d >= '1') && (d <= '4')) {
				return (d - '0');
			}
			if (d == 'P') { //bootloader will send 'P', so easy flashig without power off/on
				PORTB &= ~(1<<PB2); //heater 1 off
				PORTB &= ~(1<<PB1); //heater 2 off
				while(1); //watchdog will do it
			}
		}
		if (d == 'h') {
			puts_P(PSTR("PS/2 to USB connector\r\n"));
			return 0;
		}
		if (d == 'e') { //part of the Peda password
			g_inputEnabled = 6; //the bootloader password is 6 chars, so there will be 5 invalid.
			return 0;
		}
		if (g_inputEnabled) {
			puts_P(PSTR("False input?\r\n"));
			g_inputEnabled--;
			if (g_inputEnabled == 0) {
				puts_P(PSTR("Input disabled\r\n"));
			}
		}
	}
	return 0;
}

#endif

