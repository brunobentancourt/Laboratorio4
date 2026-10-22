#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>

#define CMD_STOP        0x00
#define CMD_START       0x01
#define CMD_SPEED       0x02
#define CMD_PISTON      0x03
#define CMD_EMERGENCY   0x04

#define LCD_ADDR        0x27 

void TWI_Init(void) {
	TWSR = 0x00;
	TWBR = 72;
}

void TWI_Start(void) {
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
}

void TWI_Stop(void) {
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

void TWI_Write(uint8_t data) {
	TWDR = data;
	TWCR = (1 << TWINT) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
}

void LCD_SendNibble(uint8_t data) {
	TWI_Start();
	TWI_Write(LCD_ADDR << 1);
	TWI_Write(data | 0x04 | 0x08);
	_delay_us(1);
	TWI_Write((data & ~0x04) | 0x08);
	TWI_Stop();
	_delay_us(50);
}

void LCD_SendByte(uint8_t val, uint8_t mode) {
	LCD_SendNibble((val & 0xF0) | mode);
	LCD_SendNibble(((val << 4) & 0xF0) | mode);
}

void LCD_Command(uint8_t cmd) { LCD_SendByte(cmd, 0x00); }
void LCD_Data(uint8_t data)   { LCD_SendByte(data, 0x01); }

void LCD_Init(void) {
	_delay_ms(50);
	LCD_Command(0x02);
	LCD_Command(0x28);
	LCD_Command(0x0C);
	LCD_Command(0x06);
	LCD_Command(0x01);
	_delay_ms(2);
}

void LCD_SetCursor(uint8_t col, uint8_t row) {
	LCD_Command((row == 0) ? (0x80 + col) : (0xC0 + col));
}

void LCD_Print(const char *str) {
	while (*str) LCD_Data(*str++);
}

void ADC_Init(void) {
	ADMUX = (1 << REFS0);
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t ADC_Read(uint8_t channel) {
	ADMUX = (ADMUX & 0xF0) | (channel & 0x07);
	ADCSRA |= (1 << ADSC);
	while (ADCSRA & (1 << ADSC));
	return ADC;
}

uint8_t DHT11_ReadTemp(uint8_t *temp) {
	uint8_t data[5] = {0};
	DDRD |= (1 << PD2);
	PORTD &= ~(1 << PD2);
	_delay_ms(18);
	PORTD |= (1 << PD2);
	_delay_us(30);
	DDRD &= ~(1 << PD2);

	_delay_us(40);
	if (PIND & (1 << PD2)) return 0;
	_delay_us(80);
	if (!(PIND & (1 << PD2))) return 0;
	while (PIND & (1 << PD2));

	for (uint8_t i = 0; i < 40; i++) {
		while (!(PIND & (1 << PD2)));
		_delay_us(30);
		if (PIND & (1 << PD2)) {
			data[i / 8] |= (1 << (7 - (i % 8)));
			while (PIND & (1 << PD2));
		}
	}
	if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
		if (data[2] > 0) *temp = data[2];
		return 1;
	}
	return 0;
}

void SPI_MasterInit(void) {
	DDRB |= (1 << PB3) | (1 << PB5) | (1 << PB2);
	PORTB |= (1 << PB2);
	SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);
}

void SPI_SendFrame(uint8_t cmd, uint8_t val) {
	PORTB &= ~(1 << PB2);
	_delay_us(20);
	SPDR = cmd;
	while (!(SPSR & (1 << SPIF)));
	_delay_us(40);
	SPDR = val;
	while (!(SPSR & (1 << SPIF)));
	_delay_us(20);
	PORTB |= (1 << PB2);
	_delay_us(50);
}

int main(void) {
	DDRD &= ~(1 << PD3);
	PORTD |= (1 << PD3);

	TWI_Init();
	LCD_Init();
	ADC_Init();
	SPI_MasterInit();

	uint8_t state = CMD_STOP;
	uint8_t piece_detected = 0;
	uint16_t piece_count = 0;
	uint8_t temp = 25;
	uint8_t speed_percent = 50;
	uint8_t last_step_delay = 0;
	uint16_t dht_timer = 0;
	char buffer[17];

	LCD_SetCursor(0, 0);
	LCD_Print("SISTEMA LISTO   ");

	while (1) {
		if (!(PIND & (1 << PD3))) {
			_delay_ms(50);
			if (!(PIND & (1 << PD3))) {
				if (state == CMD_STOP) {
					state = CMD_START;
					SPI_SendFrame(CMD_START, 0);
					} else {
					state = CMD_STOP;
					SPI_SendFrame(CMD_STOP, 0);
				}
				while (!(PIND & (1 << PD3)));
			}
		}

		uint16_t adc_pot = ADC_Read(1);
		speed_percent = (uint8_t)(((uint32_t)adc_pot * 100UL) / 1023UL);
		
		if (state == CMD_START) {
			uint8_t step_delay = 15 - ((speed_percent * 12) / 100);
			if (step_delay != last_step_delay) {
				last_step_delay = step_delay;
				SPI_SendFrame(CMD_SPEED, step_delay);
			}
		}

		uint16_t adc_ldr = ADC_Read(0);
		if (adc_ldr < 500 && !piece_detected && state == CMD_START) {
			piece_detected = 1;
			piece_count++;
			SPI_SendFrame(CMD_PISTON, 0);
			} else if (adc_ldr >= 650) {
			piece_detected = 0;
		}

		dht_timer++;
		if (dht_timer >= 20) {
			dht_timer = 0;
			DHT11_ReadTemp(&temp);
			if (temp > 40 && state != CMD_EMERGENCY) {
				state = CMD_EMERGENCY;
				SPI_SendFrame(CMD_EMERGENCY, temp);
			}
		}

		LCD_SetCursor(0, 0);
		if (state == CMD_START)      sprintf(buffer, "ST:RUN  Pz:%04d", piece_count);
		else if (state == CMD_STOP)  sprintf(buffer, "ST:STOP Pz:%04d", piece_count);
		else                         sprintf(buffer, "ST:EMERGENCIA!! ");
		LCD_Print(buffer);

		LCD_SetCursor(0, 1);
		sprintf(buffer, "T:%02dC Vel:%03d%% ", temp, speed_percent);
		LCD_Print(buffer);

		_delay_ms(100);
	}
}