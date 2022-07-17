/*
 * rmt515.c
 *
 * 2022/04/14 rev 0.1
 * Author : RJB
 * Device ATmega64
 * FUSES  EXTENDED=0xFF HIGH=0xD9 LOW=0xE4
 */ 

#include <avr/io.h>
#include <util/delay.h>

#define F_CPU 8000000UL

#define PIN_SS                    PORTB0
#define PIN_SCK                   PORTB1
#define PIN_MOSI                  PORTB2

#define ON                        1
#define OFF                       0

#define MAX7219_LOAD1             PORTB |= (1<<PIN_SS)
#define MAX7219_LOAD0             PORTB &= ~(1<<PIN_SS)

#define MAX7219_MODE_DECODE       0x09
#define MAX7219_MODE_INTENSITY    0x0A
#define MAX7219_MODE_SCAN_LIMIT   0x0B
#define MAX7219_MODE_POWER        0x0C
#define MAX7219_MODE_TEST         0x0F
#define MAX7219_MODE_NOOP         0x00

unsigned char Code7seg[16] = { 0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70,
					0x7F, 0x7B, 0x77, 0x1F, 0x4E, 0x3D, 0x4F, 0x47};

unsigned char Memory[96][3];

unsigned char Freq[3];	// Display Frequeny
unsigned char Bank;		// Memory Bank 0-5
unsigned char Num;		// Memory Number 0-F
unsigned char Preset;	// Preset Mode
unsigned char Mode;		// Direct Input Mode
unsigned char Blink;	// Blink Count
unsigned char Flash;	// Flash Count

void spiSendByte (char databyte)
{
	SPDR = databyte;
	while (!(SPSR & (1 << SPIF)));
}

void MAX7219_writeData(char data_register, char data)
{
	MAX7219_LOAD0;
	spiSendByte(data_register);
	spiSendByte(data);
	MAX7219_LOAD1;
}

void MAX7219_displayFrequency()
{
	unsigned char i,d,dp,bm,inten,f=0;
	
	bm = Mode ? Blink & 0x20 : 1;
	Blink++;
	
	if(Flash)
	{
		inten = Flash==1 ? 0x00 : 0x08;
		MAX7219_writeData(MAX7219_MODE_INTENSITY, inten);
		Flash--;
	}
	
	for( i=0; i<6; i++)
	{
		d = i%2 ? Freq[i/2] & 0x0F : Freq[i/2] >> 4;
		if( d > 0 || i==2)	f = 1;
		dp = (i==1 || i==4) ? 0x80 : 0x00; 
		if(f && bm)	MAX7219_writeData(0x08-i, Code7seg[d] + dp);
		else	MAX7219_writeData(0x08-i, 0x00);
	}
	MAX7219_writeData(0x02, Code7seg[Bank+0x0A]);
	MAX7219_writeData(0x01, Code7seg[Num]);
}

char KeyScan()
{
	unsigned char i,btn,key;
	for( i=0; i<3; i++)
	{
		PORTB &= ~(0x10<<i);
		_delay_us(100);
		btn = ~PIND & 0x3F;
		PORTB |= (0x10<<i);
		_delay_us(100);
		if(btn) break;
	}
	
	switch(0x40*i+btn)
	{
		case 0x01:	key=0x01; break; // 1
		case 0x02:	key=0x02; break; // 2
		case 0x04:	key=0x03; break; // 3
		case 0x08:	key=0x00; break; // 0
		case 0x10:	key=0x80; break; // ENTER
		case 0x20:	key=0x90; break; // PRESET

		case 0x41:	key=0x04; break; // 4
		case 0x42:	key=0x05; break; // 5
		case 0x44:	key=0x06; break; // 6
		case 0x48:	key=0xA0; break; // BANK DOWN
		case 0x50:	key=0xB0; break; // MEM DOWN

		case 0x81:	key=0x07; break; // 7
		case 0x82:	key=0x08; break; // 8
		case 0x84:	key=0x09; break; // 9
		case 0x88:	key=0xC0; break; // BANK UP
		case 0x90:	key=0xD0; break; // MEM UP
		case 0xA0:	key=0xE0; break; // MEMORY
		default:	key=0xFF;
	}
	return(key);
}

void PresetMode()
{
	if(Preset)
	{
		Preset = 0;
		DDRF = 0;
		DDRA = 0;
		DDRC = 0;
		_delay_us(1);
		PORTG = 0x01;
		PORTF = 0xFF;
		PORTA = 0xFF;
		PORTC = 0xFF;
	}
	else
	{
		Preset = 1;
		PORTG = 0x02;
		_delay_us(1);
		DDRF = 0xFF;
		DDRA = 0xFF;
		DDRC = 0xFF;
	}
}

void EEPROM_write(unsigned int uiAddress, unsigned char ucData)
{
	while(EECR & (1<<EEWE));	/* �ȑO��EEPROM�������݊����܂őҋ@ */
	EEAR = uiAddress;			/* EEPROM���ڽ�ݒ� */
	EEDR = ucData;				/* EEPROM�������ݒl��ݒ� */
	EECR |= (1<<EEMWE);			/* EEPROMϽ��������݋��� */
	EECR |= (1<<EEWE);			/* EEPROM�������݊J�n */
}

unsigned char EEPROM_read(unsigned int uiAddress)
{
	while(EECR & (1<<EEWE));	/* �ȑO��EEPROM�������݊����܂őҋ@ */
	EEAR = uiAddress;			/* EEPROM���ڽ�ݒ� */
	EECR |= (1<<EERE);			/* EEPROM�ǂݏo���J�n */
	return EEDR;				/* EEPROM�ǂݏo���l���擾,���A */
}

int main(void)
{
	unsigned char keyin,_keyin;
	unsigned char i;
	unsigned char addr;
	unsigned int eeprom_addr;
	unsigned char temp[3];
	
	for(i=0; i<96; i++)
	{
		eeprom_addr = i * 3;
		Memory[i][0] = EEPROM_read(eeprom_addr);
		Memory[i][1] = EEPROM_read(eeprom_addr+1);
		Memory[i][2] = EEPROM_read(eeprom_addr+2);
	}
	
	DDRC = 0;			// 1k, 100Hz
	DDRA = 0;			// 100k, 10k
	DDRF = 0;			// 10M, 1M
	
	PORTC = 0xFF;		// Pull up
	PORTA = 0xFF;		// Pull up
	PORTF = 0xFF;		// Pull up

	DDRG = 0x03;		// 
	PORTG = 0x01;		//
	Preset = 0;			// 
	
	DDRD = 0;
	PORTD = 0xFF;

	DDRB = 0x70 | (1 << PIN_SCK) | (1 << PIN_MOSI) | (1 << PIN_SS);
	SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);
	PORTB |= 0x70;

	MAX7219_writeData(MAX7219_MODE_TEST, 0x00);
	MAX7219_writeData(MAX7219_MODE_DECODE, 0x00);
	MAX7219_writeData(MAX7219_MODE_SCAN_LIMIT, 0x07);
	MAX7219_writeData(MAX7219_MODE_INTENSITY, 0x00);
	MAX7219_writeData(MAX7219_MODE_POWER, ON);

	Bank = 0;
	Num = 0;
	Mode = 0;
	Flash = 0;
	_keyin = 0;

	while(1)
	{
		addr = Bank * 0x10 + Num;
		eeprom_addr = addr * 3;

		if(Preset)
		{
			if(Mode)
			{
				Freq[0] = temp[0];
				Freq[1] = temp[1];
				Freq[2] = temp[2];				
			}
			else
			{
				Freq[0] = Memory[addr][0];
				Freq[1] = Memory[addr][1];
				Freq[2] = Memory[addr][2];
				PORTF = Freq[0];
				PORTA = Freq[1];
				PORTC = Freq[2];				
			}
		}
		else
		{
			Freq[0] = PINF & 0x3F;
			Freq[1] = PINA;
			Freq[2] = PINC;
			Mode = 0;
		}
		MAX7219_displayFrequency();

		keyin = KeyScan();
		if( _keyin == 0xFF)
		{
			if(keyin < 0x10)
			{
				if(Mode==1)
				{
					temp[0] = (temp[0] << 4) + (temp[1] >> 4);
					temp[1] = (temp[1] << 4) + (temp[2] >> 4);
					temp[2] = keyin << 4;
				}
				else if(Mode==2)
				{
					temp[2] = (temp[2] & 0xF0) | keyin;				
				}
				else
				{
					temp[0] = 0;
					temp[1] = 0;
					temp[2] = keyin << 4;
					Mode = 1;
				}
			}
			if(keyin == 0x80) // ENTER
			{
				if(Preset)
				{
					if(Mode)
					{
						Memory[addr][0] = temp[0];
						Memory[addr][1] = temp[1];
						Memory[addr][2] = temp[2];
						Mode = 0;
					}
					else
					{
						Memory[addr][0] = EEPROM_read(eeprom_addr);
						Memory[addr][1] = EEPROM_read(eeprom_addr+1);
						Memory[addr][2] = EEPROM_read(eeprom_addr+2);
					}					
				}
			}
			if(keyin == 0x90)	PresetMode();
			if(keyin == 0xA0)	// BANK DOWN or 100Hz in mode
			{
				if(Mode==1) Mode = 2;
				else		Bank = Bank == 0x00 ? 0x05 : Bank - 1;
			}
			if(keyin == 0xB0)	Num = Num == 0x00 ? 0x0F : Num - 1;
			if(keyin == 0xC0)	// BANK DOWN or Cancel
			{
				if(Mode)	Mode = 0;
				else		Bank = Bank == 0x05 ? 0x00 : Bank + 1;
			}
			if(keyin == 0xD0)	Num = Num == 0x0F ? 0x00 : Num + 1;
			if(keyin == 0xE0)   // Memory Write
			{
				if(Preset==0)
				{
					Memory[addr][0] = Freq[0];
					Memory[addr][1] = Freq[1];
					Memory[addr][2] = Freq[2];
					EEPROM_write( eeprom_addr, Memory[addr][0]);
					EEPROM_write( eeprom_addr+1, Memory[addr][1]);
					EEPROM_write( eeprom_addr+2, Memory[addr][2]);
					Flash = 8;
				}
				else
				{
					if(Mode==0)
					{
						EEPROM_write( eeprom_addr, Memory[addr][0]);
						EEPROM_write( eeprom_addr+1, Memory[addr][1]);
						EEPROM_write( eeprom_addr+2, Memory[addr][2]);
						Flash = 8;
					}
				}			
			}
		}
		_keyin = keyin;

		_delay_ms(50);
	}
}
