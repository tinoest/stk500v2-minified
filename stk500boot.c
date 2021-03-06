/*
Title:     STK500v2 compatible bootloader
           Heavily based on the work by Peter Fleury

Compiler:  avr-gcc 4.8.1 / avr-libc 1.4.3
Hardware:  Atmega2560/Atmega1284p
License:   BSD-3-Clause

Modified:  Martyn Brown
Date:      19 December 2017
Compiler:  avr-gcc (GCC) 4.8.1
Description:
		Heavily modified version of the stk500v2 bootloader used by Arduino,
		reduced code size and factorised where possible. Tested with latest avrdude
		to ensure no regression bugs, removed features not required and changed
		bootloader location and size.

NOTES:
    Based on Atmel Application Note AVR109 - Self-programming
    Based on Atmel Application Note AVR068 - STK500v2 Protocol
*/

#include	<stdint.h>
#include	<avr/io.h>
#include	<avr/boot.h>
#include	<avr/pgmspace.h>
#include	"command.h"

//#define REMOVE_SPI_MULTI_SUPPORT                              // disable spi multi support
//#define REMOVE_PROGRAM_LOCK_BIT_SUPPORT                       // disable program lock bits
//#define REMOVE_READ_FUSE_BIT_SUPPORT													// disable reading lock and fuse bits
//#define REMOVE_WATCHDOG_SUPPORT																// disable the clearing of the wdt bits
//#define REMOVE_READ_SIGNATURE_SUPPORT													// disable the read signature support

#ifndef EEWE
	#define EEWE    1
#endif
#ifndef EEMWE
	#define EEMWE   2
#endif

/*
 * define CPU frequency in Mhz here if not defined in Makefile
 */
#ifndef F_CPU
	#define F_CPU 16000000UL
#endif

/*
 * UART Baudrate, AVRStudio AVRISP only accepts 115200 bps
 */
#ifndef BAUDRATE
	#define BAUDRATE 115200U
#endif

#define BOOT_TIMEOUT 500000U   // should be about 1 second

/*
 *  Enable (1) or disable (0) USART double speed operation
 */
#ifndef UART_BAUDRATE_DOUBLE_SPEED
	#define UART_BAUDRATE_DOUBLE_SPEED 1U
#endif

/*
 * HW and SW version, reported to AVRISP, must match version of AVRStudio
 */
#define CONFIG_PARAM_BUILD_NUMBER_LOW		0
#define CONFIG_PARAM_BUILD_NUMBER_HIGH	0
#define CONFIG_PARAM_HW_VER							0x0F
#define CONFIG_PARAM_SW_MAJOR						2
#define CONFIG_PARAM_SW_MINOR						0x0A

/*
 * Calculate the address where the bootloader starts from FLASHEND and BOOTSIZE
 * (adjust BOOTSIZE below and BOOTLOADER_ADDRESS in Makefile if you want to change the size of the bootloader)
 * BOOTLOADER_ADDRESS (ATMega2560)	= 256 * 1024 = 262144, 2Kb bootloader, 262144 - 2048 = 260096 ( 0x3F800 )
 * BOOTLOADER_ADDRESS (ATMega1280)	= 256 * 1024 = 262144, 2Kb bootloader, 131072 - 2048 = 129024 ( 0x1F800 )
 * BOOTLOADER_ADDRESS (ATMega1284p)	= 128 * 1024 = 131072, 2Kb bootloader, 131072 - 2048 = 129024 ( 0x1F800 )
 * BOOTLOADER_ADDRESS (ATMega328pb)	= 32  * 1024 = 32768,  2Kb bootloader, 32768  - 2048 = 30720  ( 0x7800 )

 * BOOTLOADER_ADDRESS (ATMega2560)	= 256 * 1024 = 262144, 1Kb bootloader, 262144 - 1024 = 261120 ( 0x3FC00 )
 * BOOTLOADER_ADDRESS (ATMega1280)	= 256 * 1024 = 262144, 1Kb bootloader, 131072 - 1024 = 130048 ( 0x1FC00 )
 * BOOTLOADER_ADDRESS (ATMega1284p)	= 128 * 1024 = 131072, 1Kb bootloader, 131072 - 1024 = 130048 ( 0x1FC00 )
 * BOOTLOADER_ADDRESS (ATMega328pb)	= 32  * 1024 = 32768,  1Kb bootloader, 32768  - 1024 = 31744  ( 0x7C00 ) 
 */

#ifndef REMOVE_SPI_MULTI_SUPPORT
#define BOOTSIZE 2048U
#else
#define BOOTSIZE 1024U
#endif

#define APP_END  ( FLASHEND - ( 2U * BOOTSIZE ) + 1U )

/*
 * Signature bytes are not available in avr-gcc io_xxx.h
 */
#if defined (__AVR_ATmega1280__)
	#define SIGNATURE_BYTES 0x1E9703
#elif defined (__AVR_ATmega2560__)
	#define SIGNATURE_BYTES 0x1E9801
#elif defined (__AVR_ATmega1284P__)
	#define SIGNATURE_BYTES 0x1E9705
#elif defined (__AVR_ATmega328PB__)
	#define SIGNATURE_BYTES 0x1E9516
#else
	#error "no signature definition for MCU available"
#endif

#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega1284P__) || defined(__AVR_ATmega2560__)
	/* ATMega with two USART, use UART0 */
	#define	UART_BAUD_RATE_LOW			UBRR0L
	#define	UART_STATUS_REG					UCSR0A
	#define	UART_CONTROL_REG				UCSR0B
	#define	UART_ENABLE_TRANSMITTER	TXEN0
	#define	UART_ENABLE_RECEIVER		RXEN0
	#define	UART_TRANSMIT_COMPLETE	TXC0
	#define	UART_RECEIVE_COMPLETE		RXC0
	#define	UART_DATA_REG						UDR0
	#define	UART_DOUBLE_SPEED				U2X0
#elif defined(__AVR_ATmega328PB__)
	#define	UART_BAUD_RATE_LOW			UBRR0L
	#define	UART_STATUS_REG					UCSR0A
	#define	UART_CONTROL_REG				UCSR0B
	#define	UART_ENABLE_TRANSMITTER	TXEN0
	#define	UART_ENABLE_RECEIVER		RXEN0
	#define	UART_TRANSMIT_COMPLETE	TXC0
	#define	UART_RECEIVE_COMPLETE		RXC0
	#define	UART_DATA_REG						UDR0
	#define	UART_DOUBLE_SPEED				U2X0
#else
	#error "no UART definition for MCU available"
#endif

/*
 * Macro to calculate UBBR from XTAL and baudrate
 */
#if UART_BAUDRATE_DOUBLE_SPEED
	#define UART_BAUD_SELECT(baudRate, xtalCpu) (((float)(xtalCpu))/(((float)(baudRate))*8.0)-1.0+0.5)
#else
	#define UART_BAUD_SELECT(baudRate, xtalCpu) (((float)(xtalCpu))/(((float)(baudRate))*16.0)-1.0+0.5)
#endif


/*
 * States used in the receive state machine
 */
#define	ST_START			0
#define	ST_GET_SEQ_NUM	    1
#define ST_MSG_SIZE_1		2
#define ST_MSG_SIZE_2		3
#define ST_GET_TOKEN		4
#define ST_GET_DATA			5
#define	ST_GET_CHECK		6
#define	ST_PROCESS			7


/*
 * since this bootloader is not linked against the avr-gcc crt1 functions,
 * to reduce the code size, we need to provide our own initialization
 */

#ifndef REMOVE_SPI_MULTI_SUPPORT

void __jumpMain	(void) __attribute__ ((naked)) __attribute__ ((section (".init9")));
#include <avr/sfr_defs.h>

void __jumpMain(void)
{
	asm volatile ( ".set __stack, %0" :: "i" (RAMEND) );

	//	set stack pointer to top of RAM
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND >> 8) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_HI_ADDR) );

	asm volatile ( "ldi	16, %0" :: "i" (RAMEND & 0x0ff) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_LO_ADDR) );

	asm volatile ( "clr __zero_reg__" );									// GCC depends on register r1 set to 0
	asm volatile ( "out %0, __zero_reg__" :: "I" (_SFR_IO_ADDR(SREG)) );	// set SREG to 0
	asm volatile ( "jmp main");												// jump to main()

}

#else

int main(void) __attribute__ ((OS_main)) __attribute__ ((section (".init9"))) __attribute__((used));

#endif

static void readDevice(uint32_t* programAddress, uint16_t msgSize, uint8_t* p);
static void programDevice(uint32_t* programAddress, uint32_t* eraseAddress, uint16_t msgSize, uint8_t* buffer);
static int8_t serialAvailable(void);
static uint8_t recieveChar(void);
static __attribute__((noinline)) void transmitChar(int8_t c);
static uint8_t getParameter(uint8_t cmd);
static void recieveData(uint8_t* seqNum, uint8_t* msgBuffer);
void appStart(void);

static void readDevice(uint32_t* programAddress, uint16_t msgSize, uint8_t* p)
{
	uint16_t data;
	*p++						=	STATUS_CMD_OK;

	// Read FLASH
	do {
#if (FLASHEND > 0x10000)
		data	=	pgm_read_word_far(*programAddress);
#else
		data	=	pgm_read_word_near(*programAddress);
#endif
		*p++	=	(uint8_t)data;				// LSB
		*p++	=	(uint8_t)(data >> 8);	// MSB
		*programAddress	+=	2;				// Select next word in memory
		msgSize	-=	2;
	}
	while (msgSize);
}

static void programDevice(uint32_t* programAddress, uint32_t* eraseAddress, uint16_t msgSize, uint8_t* buffer)
{
	uint32_t tempAddress	=	*programAddress;

	if (*eraseAddress < APP_END )
	{
		boot_page_erase(*eraseAddress);
		boot_spm_busy_wait();
		*eraseAddress += SPM_PAGESIZE;
	}

	do {
		uint16_t word = *buffer++;
		word += (*buffer++) << 8;
		boot_page_fill(*programAddress,	word);

		*programAddress	+= 2;
		msgSize					-= 2;
	} while (msgSize);

	boot_page_write(tempAddress);
	boot_spm_busy_wait();
	boot_rww_enable();				// Re-enable the RWW section
}

/*
 * wait for data on USART
 */
static int8_t serialAvailable(void)
{

	return(UART_STATUS_REG & (1 << UART_RECEIVE_COMPLETE));	// wait for data

}

#define	MAX_TIME_COUNT	(F_CPU >> 1)

/*
 * recieve single byte from USART
 */
static uint8_t recieveChar(void)
{

	while (!(UART_STATUS_REG & (1 << UART_RECEIVE_COMPLETE)))
		;

	return UART_DATA_REG;

}

/*
 * transmit single byte to USART, wait until transmission is completed
 */
static __attribute__((noinline)) void transmitChar(int8_t c)
{

	UART_DATA_REG	=	c;

	while (!(UART_STATUS_REG & (1 << UART_TRANSMIT_COMPLETE)))
		;

	UART_STATUS_REG |= (1 << UART_TRANSMIT_COMPLETE);

}

static uint8_t getParameter(uint8_t cmd)
{
	uint8_t value;

    // If / else if / else is actually less code space than a switch
    if(cmd == PARAM_BUILD_NUMBER_LOW) {
        value	= CONFIG_PARAM_BUILD_NUMBER_LOW;
    }
		else if(cmd == PARAM_BUILD_NUMBER_HIGH) {
        value	= CONFIG_PARAM_BUILD_NUMBER_HIGH;
    }
		else if(cmd == PARAM_HW_VER) {
        value	= CONFIG_PARAM_HW_VER;
    }
		else if(cmd == PARAM_SW_MAJOR) {
        value	= CONFIG_PARAM_SW_MAJOR;
    }
		else if(cmd == PARAM_SW_MINOR) {
        value	= CONFIG_PARAM_SW_MINOR;
    }
		else {
        value	= 0;
		}

	return value;
}

static void recieveData(uint8_t* seqNum, uint8_t* buffer)
{
    uint8_t	checksum		  = 0;
    uint16_t i				  = 0;
    uint16_t length			  = 0;

	uint8_t	msgParseState;
	msgParseState	=	ST_START;
	do {
		uint8_t c	=	recieveChar();
        switch (msgParseState) {
            case ST_START:
                if ( c == MESSAGE_START ) {
                    msgParseState   = ST_GET_SEQ_NUM;
                    checksum        = MESSAGE_START ^ 0;
                }
                break;
            case ST_GET_SEQ_NUM:
                *seqNum         = c;
                msgParseState   = ST_MSG_SIZE_1;
                checksum        ^= c;
                break;

            case ST_MSG_SIZE_1:
                length       = c << 8;
                msgParseState   = ST_MSG_SIZE_2;
                checksum        ^= c;
                break;

            case ST_MSG_SIZE_2:
                length       |= c;
                msgParseState   = ST_GET_TOKEN;
                checksum        ^= c;
                break;

            case ST_GET_TOKEN:
                if ( c == TOKEN ) {
                    msgParseState   = ST_GET_DATA;
                    checksum        ^= c;
                    i               = 0;
                }
                else {
                    msgParseState   = ST_START;
                }
                break;

            case ST_GET_DATA:
                buffer[i++]      = c;
                checksum            ^= c;
                if (i == length ) {
                    msgParseState   = ST_GET_CHECK;
                }
                break;

            case ST_GET_CHECK:
                if ( c == checksum ) {
                    msgParseState   = ST_PROCESS;
                }
                else {
                    msgParseState   = ST_START;
                }
                break;
        }       //      switch
    }       //      while(msgParseState)
	while ( msgParseState != ST_PROCESS );
}

void appStart( void )
{
	uint16_t	data;

#if (FLASHEND > 0x10000)
	data	=	pgm_read_word_far(0);		//	get the first word of the user program
#else
	data	=	pgm_read_word_near(0);	//	get the first word of the user program
#endif

	if (data != 0xFFFF)	{						//	make sure its valid before jumping to it.
		asm volatile(
			"clr	r30		\n\t"
			"clr	r31		\n\t"
			"ijmp	\n\t"
		);
	}

}

//*****************************************************************************
int main(void)
{

	uint8_t *p;
	uint8_t	msgBuffer[285];
  uint8_t	seqNum				= 0;
	uint8_t ispProgram		= 0;
	uint8_t	checksum			= 0;
	uint16_t msgLength		= 0;
	uint32_t address			= 0;
	uint32_t eraseAddress	= 0;
	uint32_t bootTimer		= 0;
	uint8_t resetSource		= MCUSR;

	asm volatile ("clr __zero_reg__");

#ifndef REMOVE_WATCHDOG_SUPPORT

	__asm__ __volatile__ ("cli");
	__asm__ __volatile__ ("wdr");
	MCUSR		=	0;
	WDTCSR	|=	( 1 << WDCE ) | ( 1 << WDE );
	WDTCSR	=	0;
	__asm__ __volatile__ ("sei");

	// move the resetSource to variable r2 so we can access it in the application if we want to
	__asm__ __volatile__ ("mov r2, %0\n" :: "r" (resetSource));

#else

	MCUSR		=	0;

#endif

	// check if WDT generated the reset, if so, go straight to app
	if ( resetSource & ( 1 << WDRF ) ) {
		appStart();
	}

	/*
	 * Init UART
	 * set baudrate and enable USART receiver and transmiter without interrupts
	 */
#if UART_BAUDRATE_DOUBLE_SPEED
	UART_STATUS_REG			|=	( 1 << UART_DOUBLE_SPEED );
#endif
	UART_BAUD_RATE_LOW	=	UART_BAUD_SELECT( BAUDRATE, F_CPU );
	UART_CONTROL_REG		=	( 1 << UART_ENABLE_RECEIVER ) | ( 1 << UART_ENABLE_TRANSMITTER );

	asm volatile ("nop");			// wait until port has changed

	// wait for data
	while ( (!( serialAvailable() )) && ( ++bootTimer != BOOT_TIMEOUT ) ) {
		asm volatile ("nop");
	}

	if (bootTimer != BOOT_TIMEOUT) {
		//	main loop
		while ( ispProgram == 0 ) {
			recieveData(&seqNum, msgBuffer);	// Retrieve all the data
			// Now process the STK500 commands, see Atmel Appnote AVR068
			if(msgBuffer[0] == CMD_SIGN_ON) {
					msgLength		= 11;
					msgBuffer[1] 	= STATUS_CMD_OK;
					msgBuffer[2] 	= 8;
					msgBuffer[3] 	= 'A';
					msgBuffer[4] 	= 'V';
					msgBuffer[5] 	= 'R';
					msgBuffer[6] 	= 'I';
					msgBuffer[7] 	= 'S';
					msgBuffer[8] 	= 'P';
					msgBuffer[9] 	= '_';
					msgBuffer[10]	= '2';
			}
			else if(msgBuffer[0] == CMD_GET_PARAMETER) {
					uint8_t value;
					value					= getParameter(msgBuffer[1]);
					msgLength			= 3;
					msgBuffer[1]	= STATUS_CMD_OK;
					msgBuffer[2]	= value;
			}
			else if( ( msgBuffer[0] == CMD_LEAVE_PROGMODE_ISP ) || ( msgBuffer[0] == CMD_SET_PARAMETER ) || ( msgBuffer[0] == CMD_ENTER_PROGMODE_ISP ) ) {
					if(msgBuffer[0] == CMD_LEAVE_PROGMODE_ISP) {
							ispProgram	= 1;
					}
					msgLength			= 2;
					msgBuffer[1]	= STATUS_CMD_OK;
			}
			else if(msgBuffer[0] == CMD_LOAD_ADDRESS) {
#if defined(RAMPZ)
					address	=	((uint32_t)(msgBuffer[1]) << 24 );
					address |= ( ((uint32_t)(msgBuffer[2]) << 16 ) | ((uint32_t)(msgBuffer[3]) << 8 )|(msgBuffer[4]) ) << 1; // convert word to byte address
#else
					address	=	( ((msgBuffer[3]) << 8 ) | (msgBuffer[4]) ) << 1;		// convert word to byte address
#endif
					msgLength		= 2;
					msgBuffer[1]	= STATUS_CMD_OK;
			}
			else if(msgBuffer[0] == CMD_PROGRAM_FLASH_ISP) {
					uint16_t size	= ((msgBuffer[1]) << 8) | msgBuffer[2];
					programDevice(&address, &eraseAddress, size, msgBuffer + 10);
					msgLength		= 2;
					msgBuffer[1]	= STATUS_CMD_OK;
			}
			else if(msgBuffer[0] == CMD_READ_FLASH_ISP) {
				uint16_t size	= ((msgBuffer[1])<<8) | msgBuffer[2];
				uint8_t	*p		= msgBuffer + 1;
				msgLength		= size + 3;
				readDevice(&address, size, p);
			}

#ifndef REMOVE_READ_SIGNATURE_SUPPORT
			else if(msgBuffer[0] == CMD_READ_SIGNATURE_ISP) {
					msgLength		= 4;
					msgBuffer[1]	= STATUS_CMD_OK;
					if ( msgBuffer[4] == 0 ) {
							msgBuffer[2]    = (SIGNATURE_BYTES >> 16) & 0x000000FF;
					}
					else if ( msgBuffer[4] == 1 ) {
							msgBuffer[2]    = (SIGNATURE_BYTES >> 8) & 0x000000FF;
					}
					else {
							msgBuffer[2]    = SIGNATURE_BYTES & 0x000000FF;
					}
					msgBuffer[3]	=	STATUS_CMD_OK;
			}
#endif

#ifndef REMOVE_PROGRAM_LOCK_BIT_SUPPORT
			else if(msgBuffer[0] == CMD_READ_LOCK_ISP) {
					msgLength		= 4;
					msgBuffer[1]	= STATUS_CMD_OK;
					msgBuffer[2]	= boot_lock_fuse_bits_get( GET_LOCK_BITS );
					msgBuffer[3]	= STATUS_CMD_OK;
			}
#endif

#ifndef REMOVE_READ_FUSE_BIT_SUPPORT
			else if(msgBuffer[0] == CMD_READ_FUSE_ISP) {
					uint8_t fuseBits;
					if ( msgBuffer[2] == 0x50 ) {
							if ( msgBuffer[3] == 0x08 ) {
									fuseBits	=	boot_lock_fuse_bits_get( GET_EXTENDED_FUSE_BITS );
							}
							else {
									fuseBits	=	boot_lock_fuse_bits_get( GET_LOW_FUSE_BITS );
							}
					}
					else {
							fuseBits	=	boot_lock_fuse_bits_get( GET_HIGH_FUSE_BITS );
					}
					msgLength		=	4;
					msgBuffer[1]	=	STATUS_CMD_OK;
					msgBuffer[2]	=	fuseBits;
					msgBuffer[3]	=	STATUS_CMD_OK;
			}
#endif

#ifndef REMOVE_SPI_MULTI_SUPPORT
			else if(msgBuffer[0] == CMD_SPI_MULTI) {
					uint8_t answerByte;
					uint8_t flag=0;

					if ( msgBuffer[4]== 0x30 ) {
							uint8_t signatureIndex	=	msgBuffer[6];

							if ( signatureIndex == 0 ) {
									answerByte	=	(SIGNATURE_BYTES >> 16) & 0x000000FF;
							}
							else if ( signatureIndex == 1 ) {
									answerByte	=	(SIGNATURE_BYTES >> 8) & 0x000000FF;
							}
							else {
									answerByte	=	SIGNATURE_BYTES & 0x000000FF;
							}
					}
					else if ( msgBuffer[4] & 0x50 ) {
							if (msgBuffer[4] == 0x50) {
									answerByte	=	boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
							}
							else if (msgBuffer[4] == 0x58) {
									answerByte	=	boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
							}
							else {
									answerByte	=	0;
							}
					}
					else {
							answerByte	=	0; // for all others command are not implemented, return dummy value for AVRDUDE happy <Worapoht>
					}
					if ( flag  == 0 ) {
							msgLength			=	7;
							msgBuffer[1]	=	STATUS_CMD_OK;
							msgBuffer[2]	=	0;
							msgBuffer[3]	=	msgBuffer[4];
							msgBuffer[4]	=	0;
							msgBuffer[5]	=	answerByte;
							msgBuffer[6]	=	STATUS_CMD_OK;
					}
			}
#endif

			else {
				if(msgBuffer[0] == CMD_CHIP_ERASE_ISP) {
					eraseAddress	= 0;
				}
				msgLength			= 2;
				msgBuffer[1]	= STATUS_CMD_FAILED;
			}

			// Now send answer message back
			transmitChar(MESSAGE_START);
			checksum	=	MESSAGE_START ^ 0;

			transmitChar(seqNum);
			checksum	^=	seqNum;

			uint8_t c	=	((msgLength >> 8 ) & 0xFF);
			transmitChar(c);
			checksum	^=	c;

			c			=	msgLength & 0x00FF;
			transmitChar(c);
			checksum ^= c;

			transmitChar(TOKEN);
			checksum ^= TOKEN;

			p	=	msgBuffer;
			while ( msgLength ) {
				c	=	*p++;
				transmitChar(c);
				checksum ^= c;
				msgLength--;
			}

			transmitChar(checksum);
		}
	}

	asm volatile ("nop");			// wait until port has changed

	//Now leave bootloader
	UART_STATUS_REG	&=	0xFD;
	boot_rww_enable();				// enable application section

	appStart();

	 /*
	 * Never return to stop GCC to generate exit return code
	 * Actually we will never reach this point, but the compiler doesn't
	 * understand this
	 */
	for(;;)
		;

}
// vim: noai:ts=2:sw=2
