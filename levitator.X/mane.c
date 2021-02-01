/*
 * Levitator program
 *
 * Copyright (C) 2021 Adam Williams <broadcast at earthling dot net>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */


#include <xc.h>

// CONFIG1H
#pragma config OSC = EC         // Oscillator Selection bits (EC oscillator, CLKO function on RA6)
#pragma config FSCM = ON        // Fail-Safe Clock Monitor Enable bit (Fail-Safe Clock Monitor enabled)
#pragma config IESO = ON        // Internal External Switchover bit (Internal External Switchover mode enabled)

// CONFIG2L
#pragma config PWRT = OFF       // Power-up Timer Enable bit (PWRT disabled)
#pragma config BOR = ON         // Brown-out Reset Enable bit (Brown-out Reset enabled)
#pragma config BORV = 27        // Brown-out Reset Voltage bits (VBOR set to 2.7V)

// CONFIG2H
#pragma config WDT = ON         // Watchdog Timer Enable bit (WDT enabled)
#pragma config WDTPS = 32768    // Watchdog Timer Postscale Select bits (1:32768)

// CONFIG3H
#pragma config MCLRE = ON       // MCLR Pin Enable bit (MCLR pin enabled, RA5 input pin disabled)

// CONFIG4L
#pragma config STVR = ON        // Stack Full/Underflow Reset Enable bit (Stack full/underflow will cause Reset)
#pragma config LVP = OFF        // Low-Voltage ICSP Enable bit (Low-Voltage ICSP disabled)

// CONFIG5L
#pragma config CP0 = OFF        // Code Protection bit (Block 0 (00200-000FFFh) not code-protected)
#pragma config CP1 = OFF        // Code Protection bit (Block 1 (001000-001FFFh) not code-protected)

// CONFIG5H
#pragma config CPB = OFF        // Boot Block Code Protection bit (Boot Block (000000-0001FFh) not code-protected)
#pragma config CPD = OFF        // Data EEPROM Code Protection bit (Data EEPROM not code-protected)

// CONFIG6L
#pragma config WRT0 = OFF       // Write Protection bit (Block 0 (00200-000FFFh) not write-protected)
#pragma config WRT1 = OFF       // Write Protection bit (Block 1 (001000-001FFFh) not write-protected)

// CONFIG6H
#pragma config WRTC = OFF       // Configuration Register Write Protection bit (Configuration registers (300000-3000FFh) not write-protected)
#pragma config WRTB = OFF       // Boot Block Write Protection bit (Boot Block (000000-0001FFh) not write-protected)
#pragma config WRTD = OFF       // Data EEPROM Write Protection bit (Data EEPROM not write-protected)

// CONFIG7L
#pragma config EBTR0 = OFF      // Table Read Protection bit (Block 0 (00200-000FFFh) not protected from table reads executed in other blocks)
#pragma config EBTR1 = OFF      // Table Read Protection bit (Block 1 (001000-001FFFh) not protected from table reads executed in other blocks)

// CONFIG7H
#pragma config EBTRB = OFF      // Boot Block Table Read Protection bit (Boot Block (000000-0001FFh) not protected from table reads executed in other blocks)


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pic18f1320.h>


// clockspeed / baud rate / 4
#define BAUD_RATE_CODE 104

// -time after magnet is turned off to begin ADC
#define ADC_DELAY -7500
// target ADC value for elevation
#define TARGET_ADC 576
// max pulses with ADC below min before shutting down
#define MAX_PULSES 256
// -default time to keep magnet on
#define MAGNET_DELAY0 -15000


#define MAGNET_DISABLED 0xffff

#define MAGNET_LAT LATBbits.LATB3
#define MAGNET_TRIS TRISBbits.TRISB3

#define FAN_LAT LATAbits.LATA0
#define FAN_TRIS TRISAbits.TRISA0

// TMR0 write doesn't work
#define WRITE_TMR0(x) \
    TMR0H = (x) >> 8; \
    TMR0L = (x) & 0xff;

void magnet_off();
void magnet_on();
void adc_delay();
void start_adc();
void handle_adc();
void (*magnet_state)() = magnet_off;
// -time the magnet is on
uint16_t magnet_delay = MAGNET_DISABLED;
// total pulses with ADC below target
uint16_t pulse_count = MAX_PULSES;
// total ADC readouts since magnet was last on.  Used as lead compensation.
uint16_t adc_total = 0;

#define UART_BUFSIZE 32
uint8_t uart_buffer[UART_BUFSIZE];
uint8_t uart_size = 0;
uint8_t uart_position1 = 0;
uint8_t uart_position2 = 0;



void print_byte(uint8_t c)
{
	if(uart_size < UART_BUFSIZE)
	{
		uart_buffer[uart_position1++] = c;
		uart_size++;
		if(uart_position1 >= UART_BUFSIZE)
		{
			uart_position1 = 0;
		}
	}
}

void print_text(const uint8_t *s)
{
	while(*s != 0)
	{
		print_byte(*s);
		s++;
	}
}


void print_number_nospace(uint16_t number)
{
	if(number >= 10000) print_byte('0' + (number / 10000));
	if(number >= 1000) print_byte('0' + ((number / 1000) % 10));
	if(number >= 100) print_byte('0' + ((number / 100) % 10));
	if(number >= 10) print_byte('0' + ((number / 10) % 10));
	print_byte('0' + (number % 10));
}

void print_number2(uint8_t number)
{
	print_byte('0' + ((number / 10) % 10));
	print_byte('0' + (number % 10));
}

void print_number(uint16_t number)
{
    print_number_nospace(number);
   	print_byte(' ');
}

void print_number_signed(int16_t number)
{
    if(number < 0)
    {
        print_byte('-');
        print_number_nospace(-number);
    }
    else
    {
        print_number_nospace(number);
    }
   	print_byte(' ');
}

const char hex_table[] = 
{
	'0', '1', '2', '3', '4', '5', '6', '7', 
	'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

void print_hex2(uint8_t number)
{
	print_byte(hex_table[(number >> 4) & 0xf]);
	print_byte(hex_table[number & 0xf]);
}

void print_bin(uint8_t number)
{
	print_byte((number & 0x80) ? '1' : '0');
	print_byte((number & 0x40) ? '1' : '0');
	print_byte((number & 0x20) ? '1' : '0');
	print_byte((number & 0x10) ? '1' : '0');
	print_byte((number & 0x8) ? '1' : '0');
	print_byte((number & 0x4) ? '1' : '0');
	print_byte((number & 0x2) ? '1' : '0');
	print_byte((number & 0x1) ? '1' : '0');
}


// send a UART char
void handle_uart()
{
    if(uart_size > 0 && PIR1bits.TXIF)
    {
        PIR1bits.TXIF = 0;
        TXREG = uart_buffer[uart_position2++];
		uart_size--;
		if(uart_position2 >= UART_BUFSIZE)
		{
			uart_position2 = 0;
		}
    }
}

void flush_uart()
{
    while(uart_size > 0)
    {
        asm("clrwdt");
        handle_uart();
    }
}

// turn magnet on
void magnet_on()
{
    if(magnet_delay < MAGNET_DISABLED)
    {
        FAN_LAT = 1;
        MAGNET_LAT = 1;
        adc_total = 0;
        WRITE_TMR0(magnet_delay)
        magnet_state = magnet_off;
        INTCONbits.TMR0IF = 0;
    }
    else
    {
        start_adc();
    }
}

// turn magnet off after delay
void magnet_off()
{
    if(INTCONbits.TMR0IF)
    {
        MAGNET_LAT = 0;
        WRITE_TMR0(ADC_DELAY);
        magnet_state = adc_delay;
        INTCONbits.TMR0IF = 0;
    }
}

// wait after the magnet is turned off before starting ADC
void adc_delay()
{
    if(INTCONbits.TMR0IF)
    {
        start_adc();
    }
}

void start_adc()
{
    ADCON0bits.GO = 1;
    magnet_state = handle_adc;
    if(adc_total < 100)
    {
        adc_total++;
    }
}

void handle_adc()
{
    if(PIR1bits.ADIF)
    {
        PIR1bits.ADIF = 0;
// default state is magnet disabled
        magnet_delay = MAGNET_DISABLED;


// above target.  Reset pulse counter & disable the magnet
        if(ADRES >= TARGET_ADC)
        {
            pulse_count = 0;
            magnet_state = magnet_on;
        }
        else
        {
// below target.  Power the magnet.
            if(pulse_count < MAX_PULSES)
            {
                pulse_count++;
                magnet_delay = MAGNET_DELAY0;

// proportional feedback + lead compensation
                int16_t diff = TARGET_ADC - ADRES;
                int16_t feedback = diff * 2 /* + adc_total * 2 */;
// adjust magnet delay
                magnet_delay -= feedback;

                print_number(feedback);
                print_text("\n");
                magnet_state = magnet_on;
            }
            else
        // magnet timed out
            {
                FAN_LAT = 0;
                magnet_state = magnet_on;
            }
        }
    }
}

void main()
{
    print_text("\n\n\n\nLevitator\n");
    
    
    TXSTA = 0b00100100;
    RCSTA = 0b10000000;
    BAUDCTL = 0b0001000;
    SPBRG = BAUD_RATE_CODE;

// magnet off
    MAGNET_LAT = 0;
    MAGNET_TRIS = 0;
    FAN_LAT = 0;
    FAN_TRIS = 0;
    
    ADCON0 = 0b00001001;
    ADCON1 = 0b11111011;
    ADCON2 = 0b10111110;
    
    T0CON = 0b10000001;
    
    while(1)
    {
        asm("clrwdt");

        handle_uart();
        magnet_state();
    }
}




















