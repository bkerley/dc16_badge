/******************************************************************************
*
* DEFCON 16 BADGE
*
* Filename:		DC16.h
* Author:		  Joe Grand (Kingpin)
* Revision:		1.0
*
* Description:	Header File for the DEFCON 16 Badge (Freescale MC9S08JM60)
* Notes:
*
* Can you believe it's been a year already?
*
* SecureDigital cards MUST be formatted in FAT16. If you use FAT12 or FAT32, 
* you will end up with a corrupted FAT table. Windows automatically formats
* cards less than 64MB as FAT12, so use a bigger card to ensure that they are 
* formatted in FAT16. This will save you days of frustration. 		
*******************************************************************************/

#ifndef __DC16__
#define __DC16__

//#define _DC16_dev // uncomment this line for the DC16 development board pin-out

#include "FslTypes.h"

static const unsigned char a_word_to_the_wise[] =
{
"DEFCON 16 Badge by Joe Grand (Kingpin) \n\r\n\r\
I might be growing up, but I'm never backing down \n\r\n\r\
From corporate greed \n\r\
And authority \n\r\
From fighting for what I believe \n\r\n\r\
From my enemies \n\r\
And my family \n\r\
From society's pressure of responsibility \n\r\n\r\
From church and state \n\r\
And blind belief \n\r\
From those trying to rewrite history \n\r\n\r\
From backstabbing friends \n\r\
And snake oil fiends \n\r\
From those in my past with no integrity \n\r\n\r\
You \n\r\
   can't \n\r\
        silence \n\r\
               me. \n\r\n\r\
Goto www.kingpinempire.com\n\r\n\r\
      __     ,_. \n\r\
     /..\\    ||| FRONT ROW BADGE\n\r\
    .\\_O/    | |    LET ME IN ON THIS\n\r\
 _ /  `._    |_|\\  \n\r\
| /  \\__.`=._____\\\n\r\
|/ ._/  |\"\"\"\"\"\"\"\"\"|\n\r\
|'.  `\\ |         |\n\r\
;\"\"\"/ / |         |\n\r\
 ) /_/| |.-------.|\n\r\
'  `-`' \"         \"\n\r" 
};

/**************************************************************************
************************** Definitions ************************************
***************************************************************************/

#define LOW   0
#define HIGH  1

#define ON    0
#define OFF   1

#define MINICOM_BUFFER_SIZE 512
#define FAT16_MAX_FILE_SIZE 2147483648  // 2GB maximum FAT16 file size for 32kB clusters
 
 
#ifdef _DC16_dev  // development board      
  // inputs
  #define SW_MODE     PTGD_PTGD0
  #define USB_DETECT  PTGD_PTGD1

  // LEDs
  #define LED1  PTED_PTED0
  #define LED2  PTED_PTED2
  #define LED3  PTED_PTED3
  #define LED4  PTFD_PTFD0
  #define LED5  PTFD_PTFD1 
  #define LED6  PTFD_PTFD2
  #define LED7  PTFD_PTFD3
  #define LED8  PTED_PTED1

  #define LED1_DD  PTEDD_PTEDD0
  #define LED2_DD  PTEDD_PTEDD2
  #define LED3_DD  PTEDD_PTEDD3
  #define LED4_DD  PTFDD_PTFDD0
  #define LED5_DD  PTFDD_PTFDD1 
  #define LED6_DD  PTFDD_PTFDD2
  #define LED7_DD  PTFDD_PTFDD3
  #define LED8_DD  PTEDD_PTEDD1

  #define LED1_DS  PTEDS_PTEDS0
  #define LED2_DS  PTEDS_PTEDS2
  #define LED3_DS  PTEDS_PTEDS3
  #define LED4_DS  PTFDS_PTFDS0
  #define LED5_DS  PTFDS_PTFDS1 
  #define LED6_DS  PTFDS_PTFDS2
  #define LED7_DS  PTFDS_PTFDS3
  #define LED8_DS  PTEDS_PTEDS1
#else  // production badge
  // inputs
  #define SW_MODE     PTGD_PTGD1
  #define USB_DETECT  PTGD_PTGD0

  // LEDs
  #define LED1  PTED_PTED3
  #define LED2  PTED_PTED2
  #define LED3  PTED_PTED1
  #define LED4  PTED_PTED0
  #define LED5  PTFD_PTFD3 
  #define LED6  PTFD_PTFD2
  #define LED7  PTFD_PTFD1
  #define LED8  PTFD_PTFD0

  #define LED1_DD  PTEDD_PTEDD3
  #define LED2_DD  PTEDD_PTEDD2
  #define LED3_DD  PTEDD_PTEDD1
  #define LED4_DD  PTEDD_PTEDD0
  #define LED5_DD  PTFDD_PTFDD3 
  #define LED6_DD  PTFDD_PTFDD2
  #define LED7_DD  PTFDD_PTFDD1
  #define LED8_DD  PTFDD_PTFDD0

  #define LED1_DS  PTEDS_PTEDS3
  #define LED2_DS  PTEDS_PTEDS2
  #define LED3_DS  PTEDS_PTEDS1
  #define LED4_DS  PTEDS_PTEDS0
  #define LED5_DS  PTFDS_PTFDS3 
  #define LED6_DS  PTFDS_PTFDS2
  #define LED7_DS  PTFDS_PTFDS1
  #define LED8_DS  PTFDS_PTFDS0
#endif


/**************************************************************************
************************** Macros *****************************************
***************************************************************************/

#define F_CPU  6000000 // 6MHz bus speed (12MHz clock)
#define freq_to_timerval(x) ( F_CPU / x )

#define Sleep   _Stop;
#define Snooze  _Wait;


/**************************************************************************
************************** Structs ****************************************
***************************************************************************/

enum // error codes
{
  errSD,
  errFAT,
  errTX,
  errRX 
}; 

typedef enum // define the modes for the state machine
{
   SLEEP,
   RX, 
   TX
} state_type;

typedef enum
{
   ALL_OFF,
   ALL_ON,
   ALL_BLINK,
   KNIGHT_RIDER,
   TRANSMIT,
   BYTE,
   IGNORE 
} led_state_type;

typedef struct _codeElement {
  unsigned int onTime;   // duration of "On" time
  unsigned int offTime;  // duration of "Off" time
} codeElement;

typedef struct _powercode {
  unsigned int timer_val; // not the actual frequency, but the timer value to generate the frequency (using TPM2MOD)
  codeElement codes[100];  // maximum number of on/off codes per entry - this is horribly inefficient, but had compilation problems with codes[]
} powercode;


/***********************************************************************
 ************************** Function prototypes ************************
 ***********************************************************************/

// main.c
void DC16_RX_Mode(void);
UINT16 DC16_RX_CRC (void);
void DC16_TX_Mode(void);
void DC16_TX_File(void);
void DC16_TX_Test(void);
void DC16_Front_Row_Badge(void);
void DC16_Error(unsigned char errType, UINT16 u16Error);
void DC16_Sleep_Mode(void);
void DC16_Init(void);
void set_clock_12MHz(void);
void do_cool_LED_stuff(led_state_type type);
void delay_ms(unsigned int ms);
void delay_10us(unsigned int us);
UINT8* num2asc(UINT8 n);
UINT8 asc2num(UINT8 n_asc);
void serial_encode(char serial, powercode* appleCore);

#endif /* __DC16__ */
