#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>

#define BAUD 9600
#define MY_UBRR (F_CPU / 16 / BAUD - 1)

#define BOTON_PIN PD2
#define LED_VERDE PD3
#define LED_ROJO  PD4
#define BUZZER    PD5

typedef enum { DESARMADO, ARMADO, SILENCIADO } EstadoSistema;
typedef enum { MODO_AUTO, MODO_MANUAL } ModoOperacion;

EstadoSistema estadoActual = DESARMADO;
ModoOperacion modoActual = MODO_AUTO;

char bufferRX[32];
uint8_t indexRX = 0;
uint16_t luzBase = 0; 

void USART_Init(void) {
	UBRR0H = (unsigned char)(MY_UBRR >> 8);
	UBRR0L = (unsigned char)MY_UBRR;
	UCSR0B = (1 << RXEN0) | (1 << TXEN0);
	UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void USART_Transmit(char data) {
	while (!(UCSR0A & (1 << UDRE0)));
	UDR0 = data;
}

void USART_SendString(const char* str) {
	while (*str) {
		USART_Transmit(*str++);
	}
	USART_Transmit('\r');
	USART_Transmit('\n');
}

void initGPIO(void) {
	DDRD |= (1 << LED_VERDE) | (1 << LED_ROJO) | (1 << BUZZER);
	DDRD &= ~(1 << BOTON_PIN);
	PORTD |= (1 << BOTON_PIN);
	PORTD &= ~((1 << LED_VERDE) | (1 << LED_ROJO) | (1 << BUZZER));
}

void ADC_Init(void) {
	ADMUX = (1 << REFS0);
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t ADC_Read(void) {
	ADCSRA |= (1 << ADSC);
	while (ADCSRA & (1 << ADSC));
	return ADC;
}

void procesarComando(void) {
	
	if (strstr(bufferRX, "DESARMAR") != NULL || strstr(bufferRX, "D") != NULL) {
		estadoActual = DESARMADO;
		PORTD &= ~((1 << LED_VERDE) | (1 << LED_ROJO) | (1 << BUZZER));
		USART_SendString("ESTADO:DESARMADO");
		indexRX = 0;
		memset(bufferRX, 0, sizeof(bufferRX));
	}
	
	else if (strstr(bufferRX, "SILENCIAR") != NULL || strstr(bufferRX, "S") != NULL) {
		if (estadoActual == ARMADO || estadoActual == SILENCIADO) {
			estadoActual = SILENCIADO;
			PORTD &= ~(1 << BUZZER);
			USART_SendString("ESTADO:SILENCIADO");
		}
		indexRX = 0;
		memset(bufferRX, 0, sizeof(bufferRX));
	}
	
	else if (strstr(bufferRX, "ARMAR") != NULL || strstr(bufferRX, "A") != NULL) {
		luzBase = ADC_Read(); 
		estadoActual = ARMADO;
		PORTD |= (1 << LED_VERDE);
		PORTD &= ~((1 << LED_ROJO) | (1 << BUZZER));
		USART_SendString("ESTADO:ARMADO");
		indexRX = 0;
		memset(bufferRX, 0, sizeof(bufferRX));
	}
	
	else if (strstr(bufferRX, "MANUAL") != NULL || strstr(bufferRX, "M") != NULL) {
		modoActual = MODO_MANUAL;
		USART_SendString("MODO:MANUAL");
		indexRX = 0;
		memset(bufferRX, 0, sizeof(bufferRX));
	}
	
	else if (strstr(bufferRX, "AUTO") != NULL || strstr(bufferRX, "U") != NULL) {
		modoActual = MODO_AUTO;
		USART_SendString("MODO:AUTO");
		indexRX = 0;
		memset(bufferRX, 0, sizeof(bufferRX));
	}

	else if (strstr(bufferRX, "FORZAR_ALARMA") != NULL || strstr(bufferRX, "F") != NULL) {
		PORTD &= ~(1 << LED_VERDE);
		PORTD |= (1 << LED_ROJO) | (1 << BUZZER);
		USART_SendString("ALARMA:FORZADA_REMOTAMENTE");
		indexRX = 0;
		memset(bufferRX, 0, sizeof(bufferRX));
	}
}

int main(void) {
	initGPIO();
	USART_Init();
	ADC_Init();

	_delay_ms(500);
	USART_SendString("SISTEMA:INICIALIZADO");

	uint8_t flagEventoMovimiento = 0;
	uint8_t flagEventoVentana = 0;

	while (1) {
		
		if (UCSR0A & (1 << RXC0)) {
			char c = UDR0;
			if (c != '\r' && c != '\n') {
				if (indexRX < sizeof(bufferRX) - 1) {
					bufferRX[indexRX++] = c;
					bufferRX[indexRX] = '\0';
				}
			}
			procesarComando();
		}

		uint8_t estadoBoton = !(PIND & (1 << BOTON_PIN));
		uint16_t valorLDR = ADC_Read();
		
		
		uint8_t detencionLDR = 0;
		if (estadoActual == ARMADO) {
			if ((luzBase > 200 && valorLDR < (luzBase - 200)) || (valorLDR > (luzBase + 200))) {
				detencionLDR = 1;
			}
		}

		
		if (estadoActual == ARMADO) {
			
			
			if (estadoBoton) {
				if (modoActual == MODO_AUTO) {
					PORTD &= ~(1 << LED_VERDE);
					PORTD |= (1 << LED_ROJO) | (1 << BUZZER);
					if (!flagEventoMovimiento) {
						USART_SendString("ALARMA:ON;ZONA:MOVIMIENTO");
						flagEventoMovimiento = 1;
					}
					} else {
					if (!flagEventoMovimiento) {
						USART_SendString("ALERTA:DETECCION_MOVIMIENTO");
						flagEventoMovimiento = 1;
					}
				}
				} else {
				flagEventoMovimiento = 0;
			}

		
			if (detencionLDR) {
				if (modoActual == MODO_AUTO) {
					PORTD &= ~(1 << LED_VERDE);
					PORTD |= (1 << LED_ROJO) | (1 << BUZZER);
					if (!flagEventoVentana) {
						USART_SendString("ALARMA:ON;ZONA:VENTANA");
						flagEventoVentana = 1;
					}
					} else {
					if (!flagEventoVentana) {
						USART_SendString("ALERTA:DETECCION_VENTANA");
						flagEventoVentana = 1;
					}
				}
				} else {
				flagEventoVentana = 0;
			}
		}

		_delay_ms(20);
	}

	return 0;
}

