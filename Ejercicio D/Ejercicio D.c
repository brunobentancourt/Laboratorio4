
#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <stdint.h>
#include <stdlib.h>

//PINES
#define LED_PIN       PD6
#define JOY_X         0
#define JOY_Y         1
#define JOY_BUTTON    PD2
#define BUZZER_PIN    PB1

#define MAX_SEQUENCE  30

// Direcciones del joystick
#define UP       0
#define DOWN     1
#define LEFT     2
#define RIGHT    3

uint8_t sequence[MAX_SEQUENCE];
uint8_t sequence_length = 0;

// EEPROM: RECORD MAXIMO
uint8_t EEMEM eeprom_record = 0;
uint8_t record_max = 0;


// ADC: lectura del joystick

void adc_init(void)
{
	ADMUX = (1 << REFS0);
	ADCSRA = (1 << ADEN) |
	(1 << ADPS2) |
	(1 << ADPS1) |
	(1 << ADPS0);
}

uint16_t adc_read(uint8_t channel)
{
	ADMUX = (1 << REFS0) | (channel & 0x07);
	ADCSRA |= (1 << ADSC);

	while (ADCSRA & (1 << ADSC))
	{
	}

	return ADC;
}


// WS2812B: envio de datos

static inline void ws_send_byte(uint8_t value)
{
	for (uint8_t i = 0; i < 8; i++)
	{
		if (value & 0x80)
		{
			PORTD |= (1 << LED_PIN);

			asm volatile(
			"nop\n\t""nop\n\t""nop\n\t""nop\n\t"
			"nop\n\t""nop\n\t""nop\n\t""nop\n\t"
			::: "memory"
			);

			PORTD &= ~(1 << LED_PIN);

			asm volatile("nop\n\tnop\n\t" ::: "memory");
		}
		else
		{
			PORTD |= (1 << LED_PIN);

			asm volatile("nop\n\t" ::: "memory");

			PORTD &= ~(1 << LED_PIN);

			asm volatile(
			"nop\n\t""nop\n\t""nop\n\t""nop\n\t"
			"nop\n\t""nop\n\t""nop\n\t"
			::: "memory"
			);
		}

		value <<= 1;
	}
}

static inline void ws_pixel(uint8_t r, uint8_t g, uint8_t b)
{
	// Orden de color WS2812B: GRB
	ws_send_byte(g);
	ws_send_byte(r);
	ws_send_byte(b);
}

void matrix_clear(void)
{
	cli();

	for (uint8_t i = 0; i < 64; i++)
	{
		ws_pixel(0, 0, 0);
	}

	sei();

	PORTD &= ~(1 << LED_PIN);
	_delay_us(100);
}


// ANIMACION VISUAL DE EXITO


void success_animation(void)
{
	for (uint8_t repeat = 0; repeat < 3; repeat++)
	{
		cli();

		for (uint8_t i = 0; i < 64; i++)
		{
			ws_pixel(0, 35, 0);  // Verde
		}

		sei();

		PORTD &= ~(1 << LED_PIN);
		_delay_us(100);
		_delay_ms(150);

		matrix_clear();
		_delay_ms(150);
	}
}


// MATRIZ: triangulos por direccion

void matrix_line(uint8_t direction)
{
	cli();

	for (uint8_t y = 0; y < 8; y++)
	{
		for (uint8_t x = 0; x < 8; x++)
		{
			uint8_t r = 0;
			uint8_t g = 0;
			uint8_t b = 0;
			uint8_t encender = 0;

			switch (direction)
			{
				case UP:
				// Triangulo verde hacia arriba
				if (y <= 3 &&
				x >= (3 - y) &&
				x <= (4 + y))
				{
					encender = 1;
					g = 35;
				}
				break;

				case RIGHT:
				// Triangulo rojo hacia la derecha
				if (x >= 4 &&
				y >= (x - 4) &&
				y <= (11 - x))
				{
					encender = 1;
					r = 35;
				}
				break;

				case DOWN:
				// Triangulo azul hacia abajo
				if (y >= 4 &&
				x >= (y - 4) &&
				x <= (11 - y))
				{
					encender = 1;
					b = 35;
				}
				break;

				case LEFT:
				// Triangulo amarillo hacia la izquierda
				if (x <= 3 &&
				y >= (3 - x) &&
				y <= (4 + x))
				{
					encender = 1;
					r = 35;
					g = 20;
				}
				break;
			}

			if (!encender)
			{
				r = 0;
				g = 0;
				b = 0;
			}

			ws_pixel(r, g, b);
		}
	}

	sei();

	PORTD &= ~(1 << LED_PIN);
	_delay_us(100);
}


// BUZZER


void buzzer_init(void)
{
	DDRB |= (1 << BUZZER_PIN);
	PORTB &= ~(1 << BUZZER_PIN);
}

void beep(uint16_t frequency, uint16_t duration_ms)
{
	if (frequency == 0)
	return;

	uint32_t half_period_us = 500000UL / frequency;

	for (uint16_t i = 0; i < duration_ms; i++)
	{
		PORTB |= (1 << BUZZER_PIN);

		for (uint32_t t = 0; t < half_period_us; t++)
		_delay_us(1);

		PORTB &= ~(1 << BUZZER_PIN);

		for (uint32_t t = 0; t < half_period_us; t++)
		_delay_us(1);
	}
}

void sound_error(void)
{
	beep(250, 150);
	_delay_ms(100);
	beep(180, 200);
}

void sound_success(void)
{
	beep(700, 80);
}


// COMUNICACION SERIAL: 9600 

void uart_init(void)
{
	UBRR0H = 0;
	UBRR0L = 103;

	UCSR0A = 0;
	UCSR0B = (1 << TXEN0);
	UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void uart_send_char(char c)
{
	while (!(UCSR0A & (1 << UDRE0)))
	{
	}

	UDR0 = c;
}

void uart_print(const char *text)
{
	while (*text)
	{
		uart_send_char(*text);
		text++;
	}
}

void uart_print_number(uint8_t number)
{
	if (number >= 10)
	uart_send_char('0' + number / 10);

	uart_send_char('0' + number % 10);
}


// EEPROM: LECTURA Y ACTUALIZACION DEL RECORD


void record_init(void)
{
	record_max = eeprom_read_byte(&eeprom_record);

	// EEPROM sin inicializar o con un valor invalido
	if (record_max == 255 || record_max > MAX_SEQUENCE)
	{
		record_max = 0;
		eeprom_update_byte(&eeprom_record, record_max);
	}

	uart_print("Record maximo: ");
	uart_print_number(record_max);
	uart_print("\r\n");
}

void update_record(void)
{
	if (sequence_length > record_max)
	{
		record_max = sequence_length;

		eeprom_update_byte(&eeprom_record, record_max);

		uart_print("Record maximo: ");
		uart_print_number(record_max);
		uart_print("\r\n");
	}
}


// JOYSTICK


void joystick_init(void)
{
	DDRD &= ~(1 << JOY_BUTTON);
	PORTD |= (1 << JOY_BUTTON);
}

uint8_t read_direction(void)
{
	uint16_t x = adc_read(JOY_X);
	uint16_t y = adc_read(JOY_Y);

	// Direcciones modificadas manualmente por el usuario
	if (y > 750)
	return RIGHT;

	if (y < 270)
	return LEFT;

	if (x < 270)
	return DOWN;

	if (x > 750)
	return UP;

	return 255;
}

uint8_t button_pressed(void)
{
	return !(PIND & (1 << JOY_BUTTON));
}

void wait_for_center(void)
{
	while (read_direction() != 255)
	_delay_ms(20);
}

uint8_t get_player_direction(void)
{
	uint8_t direction = 255;

	wait_for_center();

	while (direction == 255)
	{
		direction = read_direction();
		_delay_ms(20);
	}

	matrix_line(direction);
	sound_success();

	wait_for_center();
	matrix_clear();

	return direction;
}


// JUEGO SIMON


void show_sequence(void)
{
	matrix_clear();
	_delay_ms(300);

	for (uint8_t i = 0; i < sequence_length; i++)
	{
		matrix_line(sequence[i]);
		beep(500 + (sequence[i] * 150), 100);
		_delay_ms(350);

		matrix_clear();
		_delay_ms(250);
	}
}

void new_game(void)
{
	sequence_length = 0;
	matrix_clear();

	
	srand(adc_read(JOY_X) ^ adc_read(JOY_Y));

	sequence[sequence_length++] = rand() % 4;
}

void game_over(void)
{
	sound_error();

	for (uint8_t repeat = 0; repeat < 3; repeat++)
	{
		cli();

		for (uint8_t i = 0; i < 64; i++)
		ws_pixel(40, 0, 0);

		sei();

		PORTD &= ~(1 << LED_PIN);
		_delay_us(100);
		_delay_ms(200);

		matrix_clear();
		_delay_ms(200);
	}
}


int main(void)
{
	DDRD |= (1 << LED_PIN);

	adc_init();
	buzzer_init();
	joystick_init();

	uart_init();
	record_init();

	matrix_clear();

	// Esperar a que se presione el boton del joystick
	while (!button_pressed())
	_delay_ms(20);

	_delay_ms(250);
	new_game();

	while (1)
	{
		show_sequence();

		uint8_t correct = 1;

		for (uint8_t i = 0; i < sequence_length; i++)
		{
			uint8_t answer = get_player_direction();

			if (answer != sequence[i])
			{
				correct = 0;
				break;
			}
		}

		if (!correct)
		{
			game_over();

			while (!button_pressed())
			_delay_ms(20);

			_delay_ms(250);
			new_game();
			continue;
		}

		// El record se actualiza al completar correctamente el nivel
		update_record();

		sound_success();
		success_animation();

		_delay_ms(300);

		if (sequence_length < MAX_SEQUENCE)
		{
			sequence[sequence_length] = rand() % 4;
			sequence_length++;
		}
		else
		{
			for (uint8_t i = 0; i < 3; i++)
			{
				matrix_line(UP);
				_delay_ms(150);
				matrix_line(DOWN);
				_delay_ms(150);
			}

			while (!button_pressed())
			_delay_ms(20);

			_delay_ms(250);
			new_game();
		}
	}
}
