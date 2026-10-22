#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define CMD_STOP        0x00
#define CMD_START       0x01
#define CMD_SPEED       0x02
#define CMD_PISTON      0x03
#define CMD_EMERGENCY   0x04

volatile uint8_t system_state = CMD_STOP;
volatile uint8_t step_delay_ms = 10;
volatile uint8_t trigger_piston = 0;

volatile uint8_t rx_cmd = 0;
volatile uint8_t frame_index = 0;

void Hardware_Init(void) {
	DDRD |= (1 << PD2) | (1 << PD3);
	DDRD |= (1 << PD4) | (1 << PD5) | (1 << PD6) | (1 << PD7);
	DDRB |= (1 << PB0);

	DDRB |= (1 << PB1);
	TCCR1A = (1 << COM1A1) | (1 << WGM11);
	TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);
	ICR1 = 39999;
	
	OCR1A = 2000;
}

void SPI_SlaveInit(void) {
	DDRB |= (1 << PB4);
	SPCR = (1 << SPE) | (1 << SPIE);

	PCICR |= (1 << PCIE0);
	PCMSK0 |= (1 << PCINT2);

	sei();
}

ISR(PCINT0_vect) {
	if (PINB & (1 << PB2)) {
		frame_index = 0;
	}
}

ISR(SPI_STC_vect) {
	uint8_t received_byte = SPDR;

	if (frame_index == 0) {
		rx_cmd = received_byte;
		frame_index = 1;
		} else {
		uint8_t rx_val = received_byte;
		frame_index = 0;

		if (rx_cmd == CMD_START) {
			system_state = CMD_START;
			} else if (rx_cmd == CMD_STOP) {
			system_state = CMD_STOP;
			} else if (rx_cmd == CMD_EMERGENCY) {
			system_state = CMD_EMERGENCY;
			} else if (rx_cmd == CMD_SPEED) {
			step_delay_ms = (rx_val < 3) ? 3 : rx_val;
			} else if (rx_cmd == CMD_PISTON) {
			trigger_piston = 1;
		}
	}
}

void ActuatePiston(void) {
	OCR1A = 3800;
	_delay_ms(800);
	
	OCR1A = 2000;
	_delay_ms(200);
	
	trigger_piston = 0;
}

const uint8_t step_masks[4] = {
	(1 << PD4),
	(1 << PD5),
	(1 << PD6),
	(1 << PD7)
};

int main(void) {
	Hardware_Init();
	SPI_SlaveInit();

	uint8_t current_step = 0;
 
	while (1) {
		if (system_state == CMD_START) {
			PORTD |= (1 << PD2);
			PORTD &= ~(1 << PD3);
			PORTB &= ~(1 << PB0);

			PORTD = (PORTD & 0x0F) | step_masks[current_step];
			current_step = (current_step + 1) % 4;

			for (uint8_t i = 0; i < step_delay_ms; i++) {
				_delay_ms(1);
			}

			if (trigger_piston) {
				ActuatePiston();
			}

			} else if (system_state == CMD_STOP) {
			PORTD &= ~((1 << PD2) | (1 << PD3) | (1 << PD4) | (1 << PD5) | (1 << PD6) | (1 << PD7));
			PORTB &= ~(1 << PB0);
			_delay_ms(50);

			} else if (system_state == CMD_EMERGENCY) {
			PORTD &= ~((1 << PD2) | (1 << PD4) | (1 << PD5) | (1 << PD6) | (1 << PD7));
			
			PORTD ^= (1 << PD3);
			PORTB ^= (1 << PB0);
			_delay_ms(200);
		}
	}
}