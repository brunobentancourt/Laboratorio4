#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define EEPROM_MAGIC_ADDR 0x0000
#define EEPROM_MAGIC_KEY  0x82
#define NUM_CUENTAS       4

typedef struct {
	uint8_t uid[4];
	char pin[5];
	uint32_t saldo;
} Cuenta;

Cuenta cuentas[NUM_CUENTAS];
int8_t cuentaActiva = -1;
uint8_t intentosPIN = 0;

#define LED_VERDE_PIN PC1
#define LED_ROJO_PIN  PC2
#define BUZZER_PIN    PC3

#define RC522_RST_PIN PB1
#define RC522_SS_PIN  PB2
#define SPI_MOSI_PIN  PB3
#define SPI_MISO_PIN  PB4
#define SPI_SCK_PIN   PB5

#define LCD_I2C_ADDR  0x4E

void TWI_init(void) {
	TWSR = 0x00;
	TWBR = 72;
}

void TWI_start(void) {
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
}

void TWI_stop(void) {
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

void TWI_write(uint8_t data) {
	TWDR = data;
	TWCR = (1 << TWINT) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
}

uint8_t lcd_backlight = 0x08;

void LCD_send_pcf(uint8_t data) {
	TWI_start();
	TWI_write(LCD_I2C_ADDR);
	TWI_write(data | lcd_backlight);
	TWI_stop();
}

void LCD_pulse(uint8_t data) {
	LCD_send_pcf(data | 0x04);
	_delay_us(1);
	LCD_send_pcf(data & ~0x04);
	_delay_us(50);
}

void LCD_send_nibble(uint8_t nibble, uint8_t rs) {
	uint8_t data = (nibble & 0xF0) | (rs ? 0x01 : 0x00);
	LCD_send_pcf(data);
	LCD_pulse(data);
}

void LCD_send_byte(uint8_t val, uint8_t rs) {
	LCD_send_nibble(val & 0xF0, rs);
	LCD_send_nibble((val << 4) & 0xF0, rs);
}

void LCD_command(uint8_t cmd) { LCD_send_byte(cmd, 0); }
void LCD_char(char ch)        { LCD_send_byte(ch, 1); }

void LCD_init(void) {
	TWI_init();
	_delay_ms(50);
	LCD_send_nibble(0x30, 0); _delay_ms(5);
	LCD_send_nibble(0x30, 0); _delay_us(150);
	LCD_send_nibble(0x30, 0);
	LCD_send_nibble(0x20, 0);

	LCD_command(0x28);
	LCD_command(0x0C);
	LCD_command(0x06);
	LCD_command(0x01);
	_delay_ms(2);
}

void LCD_clear(void) {
	LCD_command(0x01);
	_delay_ms(2);
}

void LCD_set_cursor(uint8_t row, uint8_t col) {
	uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
	LCD_command(addr);
}

void LCD_print(const char* str) {
	while (*str) LCD_char(*str++);
}

void SPI_init(void) {
	DDRB |= (1 << SPI_MOSI_PIN) | (1 << SPI_SCK_PIN) | (1 << RC522_SS_PIN) | (1 << RC522_RST_PIN);
	DDRB &= ~(1 << SPI_MISO_PIN);
	PORTB |= (1 << RC522_SS_PIN);
	SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);
}

uint8_t SPI_transfer(uint8_t data) {
	SPDR = data;
	while (!(SPSR & (1 << SPIF)));
	return SPDR;
}

void RC522_write_reg(uint8_t reg, uint8_t val) {
	PORTB &= ~(1 << RC522_SS_PIN);
	SPI_transfer((reg << 1) & 0x7E);
	SPI_transfer(val);
	PORTB |= (1 << RC522_SS_PIN);
}

uint8_t RC522_read_reg(uint8_t reg) {
	uint8_t val;
	PORTB &= ~(1 << RC522_SS_PIN);
	SPI_transfer(((reg << 1) & 0x7E) | 0x80);
	val = SPI_transfer(0x00);
	PORTB |= (1 << RC522_SS_PIN);
	return val;
}

void RC522_init(void) {
	PORTB &= ~(1 << RC522_RST_PIN);
	_delay_ms(20);
	PORTB |= (1 << RC522_RST_PIN);
	_delay_ms(50);

	RC522_write_reg(0x01, 0x0F);
	_delay_ms(50);

	RC522_write_reg(0x2A, 0x8D);
	RC522_write_reg(0x2B, 0x3E);
	RC522_write_reg(0x2D, 30);
	RC522_write_reg(0x2C, 0);
	RC522_write_reg(0x15, 0x40);
	RC522_write_reg(0x11, 0x3D);

	uint8_t reg = RC522_read_reg(0x14);
	if ((reg & 0x03) != 0x03) {
		RC522_write_reg(0x14, reg | 0x03);
	}
}

uint8_t RC522_to_card(uint8_t cmd, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen) {
	uint8_t status = 0;
	uint8_t irqEn = 0x77;
	uint8_t waitIRq = 0x30;
	uint8_t n;
	uint16_t i;

	RC522_write_reg(0x02, irqEn | 0x80);
	RC522_write_reg(0x04, 0x7F);
	RC522_write_reg(0x0A, 0x80);
	RC522_write_reg(0x01, 0x00);

	for (i = 0; i < sendLen; i++) {
		RC522_write_reg(0x09, sendData[i]);
	}

	RC522_write_reg(0x01, cmd);
	if (cmd == 0x0C) {
		RC522_write_reg(0x0D, RC522_read_reg(0x0D) | 0x80);
	}

	i = 2000;
	do {
		n = RC522_read_reg(0x04);
		i--;
	} while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

	RC522_write_reg(0x0D, RC522_read_reg(0x0D) & (~0x80));

	if (i != 0) {
		if (!(RC522_read_reg(0x06) & 0x1B)) {
			status = 1;
			if (cmd == 0x0C) {
				n = RC522_read_reg(0x0A);
				uint8_t lastBits = RC522_read_reg(0x0C) & 0x07;
				if (lastBits) *backLen = (n - 1) * 8 + lastBits;
				else *backLen = n * 8;

				if (n == 0) n = 1;
				if (n > 16) n = 16;
				for (i = 0; i < n; i++) {
					backData[i] = RC522_read_reg(0x09);
				}
			}
		}
	}
	return status;
}

bool RC522_get_uid(uint8_t *uid) {
	uint8_t req[1] = {0x52};
	uint8_t backData[16];
	uint16_t backLen;

	RC522_write_reg(0x0D, 0x07);

	if (RC522_to_card(0x0C, req, 1, backData, &backLen) != 1) {
		req[0] = 0x26;
		if (RC522_to_card(0x0C, req, 1, backData, &backLen) != 1) {
			return false;
		}
	}

	if (backLen == 0x10) {
		RC522_write_reg(0x0D, 0x00);
		uint8_t anti[2] = {0x93, 0x20};
		if (RC522_to_card(0x0C, anti, 2, backData, &backLen) == 1) {
			for (uint8_t i = 0; i < 4; i++) {
				uid[i] = backData[i];
			}
			return true;
		}
	}
	return false;
}

void Keypad_init(void) {
	DDRD |= (1 << PD2) | (1 << PD3) | (1 << PD4) | (1 << PD5);
	PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4) | (1 << PD5);

	DDRD &= ~((1 << PD6) | (1 << PD7));
	PORTD |= (1 << PD6) | (1 << PD7);

	DDRB &= ~(1 << PB0);
	PORTB |= (1 << PB0);

	DDRC &= ~(1 << PC0);
	PORTC |= (1 << PC0);
}

char Keypad_get_key(void) {
	char map[4][4] = {
		{'1','2','3','A'},
		{'4','5','6','B'},
		{'7','8','9','C'},
		{'*','0','#','D'}
	};

	for (uint8_t r = 0; r < 4; r++) {
		PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4) | (1 << PD5);
		PORTD &= ~(1 << (PD2 + r));
		_delay_us(10);

		if (!(PIND & (1 << PD6))) { while (!(PIND & (1 << PD6))); return map[r][0]; }
		if (!(PIND & (1 << PD7))) { while (!(PIND & (1 << PD7))); return map[r][1]; }
		if (!(PINB & (1 << PB0))) { while (!(PINB & (1 << PB0))); return map[r][2]; }
		if (!(PINC & (1 << PC0))) { while (!(PINC & (1 << PC0))); return map[r][3]; }
	}
	return 0;
}

void senal_ok(void) {
	PORTC |= (1 << LED_VERDE_PIN) | (1 << BUZZER_PIN);
	_delay_ms(150);
	PORTC &= ~(1 << BUZZER_PIN);
	_delay_ms(350);
	PORTC &= ~(1 << LED_VERDE_PIN);
}

void senal_error(void) {
	PORTC |= (1 << LED_ROJO_PIN);
	for (uint8_t i = 0; i < 2; i++) {
		PORTC |= (1 << BUZZER_PIN);
		_delay_ms(100);
		PORTC &= ~(1 << BUZZER_PIN);
		_delay_ms(100);
	}
	PORTC &= ~(1 << LED_ROJO_PIN);
}

void senal_alarma(void) {
	PORTC |= (1 << LED_ROJO_PIN);
	for (uint8_t i = 0; i < 6; i++) {
		PORTC |= (1 << BUZZER_PIN);
		_delay_ms(200);
		PORTC &= ~(1 << BUZZER_PIN);
		_delay_ms(100);
	}
	PORTC &= ~(1 << LED_ROJO_PIN);
}

void cargar_EEPROM(void) {
	eeprom_read_block((void*)cuentas, (const void*)(EEPROM_MAGIC_ADDR + 1), sizeof(cuentas));
}

void guardar_cuenta_EEPROM(uint8_t idx) {
	uint16_t offset = (EEPROM_MAGIC_ADDR + 1) + (idx * sizeof(Cuenta));
	eeprom_update_block((const void*)&cuentas[idx], (void*)offset, sizeof(Cuenta));
}

void inicializar_cuentas_defecto(void) {
	Cuenta c1 = {{0x00, 0x00, 0x01, 0x59}, "1234", 1000};
	Cuenta c2 = {{0x43, 0x57, 0xB9, 0x29}, "4321", 500};
	Cuenta c3 = {{0xE3, 0x76, 0x09, 0x2D}, "1111", 2500};
	Cuenta c4 = {{0xC3, 0x50, 0xB9, 0x14}, "5555", 3000};

	cuentas[0] = c1;
	cuentas[1] = c2;
	cuentas[2] = c3;
	cuentas[3] = c4;

	for (uint8_t i = 0; i < NUM_CUENTAS; i++) {
		guardar_cuenta_EEPROM(i);
	}
	eeprom_write_byte((uint8_t*)EEPROM_MAGIC_ADDR, EEPROM_MAGIC_KEY);
}

int8_t buscar_cuenta(uint8_t *uid) {
	for (uint8_t i = 0; i < NUM_CUENTAS; i++) {
		if (memcmp(cuentas[i].uid, uid, 4) == 0) {
			return i;
		}
	}
	return -1;
}

typedef enum {
	ESPERA_TARJETA,
	INGRESO_PIN,
	MENU_PRINCIPAL,
	CONSULTA_SALDO,
	RETIRO_MONTO,
	DEPOSITO_MONTO
} EstadoFSM;

void mostrar_espera(void) {
	LCD_clear();
	LCD_print("  CAJERO AUTOM  ");
	LCD_set_cursor(1, 0);
	LCD_print("Acerque Tarjeta");
}

void mostrar_menu(void) {
	LCD_clear();
	LCD_print("1:Sal 2:Ret 3:Dep");
	LCD_set_cursor(1, 0);
	LCD_print("*:Salir");
}

uint32_t leer_monto_teclado(void) {
	char buffer[7] = "";
	uint8_t pos = 0;

	while (1) {
		char k = Keypad_get_key();
		if (k >= '0' && k <= '9') {
			if (pos < 6) {
				buffer[pos++] = k;
				buffer[pos] = '\0';
				LCD_char(k);
			}
			} else if (k == '#') {
			break;
			} else if (k == '*') {
			return 0;
		}
	}
	uint32_t val = 0;
	sscanf(buffer, "%lu", &val);
	return val;
}

int main(void) {
	DDRC |= (1 << LED_VERDE_PIN) | (1 << LED_ROJO_PIN) | (1 << BUZZER_PIN);
	PORTC &= ~((1 << LED_VERDE_PIN) | (1 << LED_ROJO_PIN) | (1 << BUZZER_PIN));

	LCD_init();
	SPI_init();
	RC522_init();
	Keypad_init();

	uint8_t version = RC522_read_reg(0x37);
	LCD_clear();
	if (version == 0x91 || version == 0x92) {
		LCD_print("Lector RFID OK");
		} else {
		LCD_print("SDA/SCK/MOSI Err");
	}
	_delay_ms(1200);

	if (eeprom_read_byte((uint8_t*)EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_KEY) {
		inicializar_cuentas_defecto();
		} else {
		cargar_EEPROM();
	}

	EstadoFSM estado = ESPERA_TARJETA;
	mostrar_espera();

	uint8_t uid_leido[4];
	char pin_buf[5];
	uint8_t pin_pos = 0;

	while (1) {
		switch (estado) {

			case ESPERA_TARJETA:
			if (RC522_get_uid(uid_leido)) {
				cuentaActiva = buscar_cuenta(uid_leido);
				if (cuentaActiva != -1) {
					intentosPIN = 0;
					estado = INGRESO_PIN;
					pin_pos = 0;
					
					senal_ok();
					LCD_clear();
					LCD_print("Tarjeta Valida");
					LCD_set_cursor(1, 0);
					LCD_print("PIN: ");
					} else {
					char uid_str[16];
					snprintf(uid_str, sizeof(uid_str), "%02X %02X %02X %02X",
					uid_leido[0], uid_leido[1], uid_leido[2], uid_leido[3]);
					
					LCD_clear();
					LCD_print("No Registrada!");
					LCD_set_cursor(1, 0);
					LCD_print(uid_str);
					
					senal_error();
					_delay_ms(2500);
					mostrar_espera();
				}
			}
			_delay_ms(100);
			break;

			case INGRESO_PIN: {
				char k = Keypad_get_key();
				if (k >= '0' && k <= '9') {
					pin_buf[pin_pos++] = k;
					LCD_char('*');
					if (pin_pos == 4) {
						pin_buf[4] = '\0';
						if (strcmp(pin_buf, cuentas[cuentaActiva].pin) == 0) {
							senal_ok();
							estado = MENU_PRINCIPAL;
							mostrar_menu();
							} else {
							intentosPIN++;
							senal_error();
							if (intentosPIN >= 3) {
								LCD_clear();
								LCD_print("3 Intentos Err!");
								senal_alarma();
								cuentaActiva = -1;
								estado = ESPERA_TARJETA;
								mostrar_espera();
								} else {
								pin_pos = 0;
								LCD_clear();
								LCD_print("PIN Incorrecto!");
								_delay_ms(1000);
								LCD_clear();
								LCD_print("PIN: ");
							}
						}
					}
					} else if (k == '*') {
					estado = ESPERA_TARJETA;
					mostrar_espera();
				}
				break;
			}

			case MENU_PRINCIPAL: {
				char k = Keypad_get_key();
				if (k == '1') estado = CONSULTA_SALDO;
				else if (k == '2') estado = RETIRO_MONTO;
				else if (k == '3') estado = DEPOSITO_MONTO;
				else if (k == '*') {
					cuentaActiva = -1;
					estado = ESPERA_TARJETA;
					mostrar_espera();
				}
				break;
			}

			case CONSULTA_SALDO: {
				char str_buf[16];
				LCD_clear();
				LCD_print("Saldo Actual:");
				LCD_set_cursor(1, 0);
				snprintf(str_buf, sizeof(str_buf), "$ %lu", cuentas[cuentaActiva].saldo);
				LCD_print(str_buf);
				_delay_ms(3000);
				estado = MENU_PRINCIPAL;
				mostrar_menu();
				break;
			}

			case RETIRO_MONTO: {
				LCD_clear();
				LCD_print("Monto Retiro:");
				LCD_set_cursor(1, 0);
				LCD_print("$ ");
				
				uint32_t monto = leer_monto_teclado();
				if (monto > 0 && monto <= cuentas[cuentaActiva].saldo) {
					cuentas[cuentaActiva].saldo -= monto;
					guardar_cuenta_EEPROM(cuentaActiva);
					LCD_clear();
					LCD_print("Retiro OK!");
					senal_ok();
					} else {
					LCD_clear();
					LCD_print("Saldo Insufic.!");
					senal_error();
				}
				_delay_ms(2000);
				estado = MENU_PRINCIPAL;
				mostrar_menu();
				break;
			}
			
			case DEPOSITO_MONTO: {
				LCD_clear();
				LCD_print("Monto Deposito:");
				LCD_set_cursor(1, 0);
				LCD_print("$ ");

				uint32_t monto = leer_monto_teclado();
				if (monto > 0) {
					cuentas[cuentaActiva].saldo += monto;
					guardar_cuenta_EEPROM(cuentaActiva);
					LCD_clear();
					LCD_print("Deposito OK!");
					senal_ok();
					} else {
					LCD_clear();
					LCD_print("Monto Invalido!");
					senal_error();
				}
				_delay_ms(2000);
				estado = MENU_PRINCIPAL;
				mostrar_menu();
				break;
			} 
		}
	}
	return 0;
}