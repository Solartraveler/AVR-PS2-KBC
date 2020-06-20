
#ifndef RS232_H
#define RS232_H

#include <stdio.h>

#ifndef NODEBUGOUTPUT

void uart_init(void);

uint8_t rs232_key(void);

#else
static __inline__ uint8_t rs232_key(void) {
	return 0;
}

static __inline__ void uart_init(void) {

}

#endif

#endif
