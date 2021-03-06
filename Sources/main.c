/******************************************************************************
*
* DEFCON 16 BADGE
*
* Filename:		main.c
* Author:		  Joe Grand (Kingpin)
* Revision:		1.0
* Last Updated: June 30, 2008
*
* Description:	Main File for the DEFCON 16 Badge (Freescale MC9S08JM60)
* Notes:
*
* See DC16.h for more inf0z...		
*******************************************************************************/

#include <hidef.h>          /* for EnableInterrupts macro */
#include <string.h>
#include <ctype.h>
#include "usb.h"
#include "target.h"
#include "usb_cdc.h"
#include "utils.h"
#include "FslTypes.h"
#include "Fat.h"
#include "SD.h"
#include "ir.h"
#include "crc16.h"
#include "DC16.h"


/****************************************************************************
 ************************** Global variables ********************************
 ***************************************************************************/
 
UINT8 gu8IntFlags;
UINT8 gau8Minicom[MINICOM_BUFFER_SIZE]; // buffer for SD card incoming/outgoing user data
state_type state = RX; // set power-on state
led_state_type led_state = KNIGHT_RIDER;
UINT8 state_change_flag = 0; // if set, we need to change the state/mode
UINT8 power_on_flag = 1;  // if set, we have just powered up and changing state for the first time
UINT8 usb_enabled_flag = 0; // if not set, usb needs to be initialized
UINT8 ir_rx_byte = FALSE; // if set, we are receiving a byte via IR
UINT8 counter = 0; // counter for RTC
UINT16 timeout = 0; // timeout counter for file transfers
UINT8 gu8Led; // this byte is displayed on the LEDs if the led_state = BYTE
UINT8 n_str[3]; // temp. for num2asc and asc2num conversion functions

// from tv_b_gone.c
extern powercode *powerCodes[];
extern unsigned char num_codes;

// from ir.c
extern unsigned char ir_command;
extern unsigned char ir_address;
extern unsigned char ir_state;

// from Fat.c
extern ReadRHandler RHandler;


/****************************************************************************
 ************************** Function definitions ****************************
 ***************************************************************************/
void main(void)
{
  int cdc_in;
  int uart_in;
  
  DC16_Init();
  
  delay_ms(100);
  do_cool_LED_stuff(ALL_ON); // power-on test
  DC16_TX_Test();
  do_cool_LED_stuff(ALL_OFF);
 
  cdc_in=uart_in=0xff+1; // USB CMX init
  cdc_init();
   
  // sorry for this spaghetti-like state machine - i'm just a lowly electrical engineer :)
  while(1)
  {    
    // initialize USB module if it hasn't started yet or has been shutdown
    // this will save power if the USB module isn't needed or used...
    if (!usb_enabled_flag && USB_DETECT)  // when USB plugged in...
    {
      hw_init(); // setup usb & 48MHz clock using 12MHz external crystal (from \USB_CMX\target.c) 
      usb_cfg_init();     
      SPI2BR = 0x11; // SPI baud rate, 3MHz @ 24MHz bus clock
      TPM2SC_PS = 0b010;    // TPM2 timer prescaler = /4
      usb_enabled_flag = 1;
    }
    else if (usb_enabled_flag && !USB_DETECT) // when USB removed, reduce clock speed to 12MHz...
    {
      usb_stop();
      set_clock_12MHz();
      usb_enabled_flag = 0;
    }
  
    if (!SW_MODE) // if button is pressed...
    {
      delay_ms(50); // poor man's debounce
      
      while (!SW_MODE) // while button is held down...
      {
        state_change_flag = 1;
        led_state = ALL_OFF;
      }  
    }
     
    if (state_change_flag) // if there has been a change in state
    {
      if (power_on_flag && usb_enabled_flag && USB_DETECT) // this is only run once at power-on if USB is ready
      {
        power_on_flag = 0;
        Terminal_Send_String("\n\r");
        Terminal_Send_String((unsigned char*)a_word_to_the_wise);
        Terminal_Send_String("\n\r\n\rWelcome to the debug terminal...\n\r\n\r");
      }
      
    	state_change_flag = 0;
  	  switch (state) // ...then set the new state
    	{
    	  case SLEEP:
  	      state = RX;
  	      led_state = KNIGHT_RIDER;
  	      if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("Entering RECEIVE mode.\n\r");
  	      break;
    	  case RX:
    	    state = TX;
    	    led_state = TRANSMIT;
    	    if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("Entering TRANSMIT mode.\n\r");
    	    break;
    	  case TX:
    	  default:
    	    state = SLEEP;
    	    led_state = ALL_OFF;
    	    if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("Going to SLEEP.\n\r");
    	    break;
    	}
    }
    
    switch (state)
    {
      case RX:
        DC16_RX_Mode();
        break;
      case TX:
        DC16_TX_Mode();
        break;
      case SLEEP:   // sleepy time!
      default:
        DC16_Sleep_Mode();
    }
  }
}


/********************************************************/
void DC16_RX_Mode (void)
{
  unsigned int i, j, k;
  volatile UINT16 u16Error;
  UINT16 u16CRC;
  UINT32 fileSize, progressCount, ledcnt;

  RTCSC_RTCPS = 0xA; // re-enable RTC (set prescaler = /4, interrupt every 4ms)
  IR_TX_Off;
      
  // enable IR receive mode
  // prepare TPM2 to be used for IR TPM2MOD = IR_RC_PERIOD; TPM2C1 as falling edge
  ir_init(IR_RC_PERIOD);    
 
  (void)memset(gau8Minicom, 0x00, MINICOM_BUFFER_SIZE); // clear buffer

  // wait to start receiving data
  // and when we do, get filename (should be in 8.3 format + NULL)
  k = 0; 
  do
  {
    while (!ir_rx_byte) // wait to start receiving IR data...
    {
      if (SD_PRESENT == HIGH) // if no SD card, exit mode
      {
        delay_ms(500);
        return;
      }
        
      if (!SW_MODE)
      {
        delay_ms(50); // poor man's debounce
          
        while (!SW_MODE)
        {
          IR_RX_Off; // disable RX when we're done
          led_state = ALL_OFF;
          state_change_flag = 1; // go to the TX state, since we haven't started receiving yet
       }
        return; // get outta here!
      }
    }

    // if we get here, that means we've received a valid byte
    ir_rx_byte = FALSE;

    gau8Minicom[k] = ir_command;
    ++k;
  } while ((ir_command != '\0') && (k < 13));
  gau8Minicom[k-1] = '\0'; // add a null in case we've received incorrect filename data

  if (gau8Minicom[0] == '\0') // if no filename is received, abort transfer
  {
      if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("Filename invalid.\n\r");
      DC16_Error(errRX, 0);
      return;     
  }
  
  // ensure the filename is uppercase before working with it
  // gotta love FAT16!
  for (j = 0; gau8Minicom[j] != '\0'; ++j)
    gau8Minicom[j] = (UINT8)toupper(gau8Minicom[j]);
     
  // get file size (4 bytes long)
  fileSize = 0;
  for (i = 0; i < 4; ++i)
  {
    timeout = 0;
    while (!ir_rx_byte) // wait to start receiving IR data...
    {
      if (timeout == 1250) // if we haven't received a byte for ~5 seconds, abort
      {             
        DC16_Error(errRX, 0);
        return;                          
      }
        
      if (!SW_MODE)
      {
        delay_ms(50); // poor man's debounce
          
        while (!SW_MODE)
        {
          IR_RX_Off; // disable RX when we're done
          led_state = ALL_OFF;
          state = TX; // go to SLEEP mode to avoid sending data as soon as we cancel a receive
          state_change_flag = 1;
        }
        return; // get outta here!
      }
    }

    // if we get here, that means we've received a valid byte
    ir_rx_byte = FALSE;

    fileSize <<= 8;
    fileSize |= ir_command; 
  }
  
  if (fileSize == 0xFFFFFFFF) // we get this if one badge receives another badge's power on IR test string (the Sony TV power off code), so just ignore it
  {
    delay_ms(500);
    return;
  }
  //else if ((fileSize > FAT16_MAX_FILE_SIZE) || (fileSize == 0)) // if the received file size is bigger than the possible FAT16 limit, we obviously have a corrupted transfer
  else if ((fileSize > 131072) || (fileSize == 0)) // limit filesize to 128KB to reduce transfer errors and prevent people from standing in front of each other for hours!
  {
     // so abort...
     if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("Filesize error!\n\r");
     DC16_Error(errRX, 0);
     return;        
  }
  
  // if the filename and file size appear to be valid, we can now proceed...
  if (SD_PRESENT != HIGH)
  {
    u16Error=SD_Init(); // SD card init
    if (u16Error == OK) 
    {
      FAT_Read_Master_Block(); // FAT driver init
    }
    else 
    {
      DC16_Error(errSD, u16Error);
      return;
    }
    
    // see if a file already exists with the name we've just received
    u16Error=FAT_FileOpen(gau8Minicom, READ); 
    if (u16Error == FILE_FOUND) 
    { 
      for (j = '0'; j <= '9'; ++j)
      {
        // replace the last character in the filename with a numeral
        // this will let us receive 10 more files with the same name (0-9) 
        gau8Minicom[k-2] = (UINT8)j; // k is the index of the filename string         
        u16Error=FAT_FileOpen(gau8Minicom, READ); 
    
        if (u16Error != FILE_FOUND) break; // once we've found a filename that doesn't exist, we can move on...
      }
    }
          
    if (WP_ENABLED == HIGH)
    {
      if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("SD card write protect enabled.\n\r");
      DC16_Error(errRX, 0);
      return;    
    }
    else
    {
      // create a new file on the SD card
      u16Error=FAT_FileOpen(gau8Minicom, CREATE); 
      if (u16Error != FILE_CREATE_OK) 
      { 
        DC16_Error(errFAT, u16Error);
        return;
      }
    }
  }
  else
  {
    if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("No SD card inserted.\n\r");
    state = TX; // go to SLEEP mode to avoid receiving unsynchronized data
    state_change_flag = 1;
    return;
  }
  
  if (usb_enabled_flag && USB_DETECT)
  { 
    Terminal_Send_String("File name: ");
    Terminal_Send_String(gau8Minicom);
    Terminal_Send_String("\n\r");
     
    itoa(fileSize, gau8Minicom, 12);      
    Terminal_Send_String("File size: ");
    Terminal_Send_String(gau8Minicom);
    Terminal_Send_String(" bytes\n\r");
    Terminal_Send_String("Starting IR file receive.\n\r");
  }
  
  led_state = IGNORE; // set this so the timer interrupt routine doesn't mess with our LED output
  do_cool_LED_stuff(ALL_OFF); // we're going to use LEDs as a transfer progress indicator, so we want complete control

  progressCount = fileSize >> 3;
  ledcnt = 0;
  gu8Led = 0x00;
  
  if (fileSize >= MINICOM_BUFFER_SIZE)
  {
    do
    {
      // assuming each block sent is MINICOM_BUFFER_SIZE (512 bytes), except for the last one which might be shorter
      for (i = 0; i < MINICOM_BUFFER_SIZE; ++i) 
      {  
        timeout = 0;  
        while (!ir_rx_byte) // wait to start receiving IR data...
        {
          if (timeout == 1250) // if we haven't received a byte for ~5 seconds, abort
          {
            FAT_FileClose();
            DC16_Error(errRX, 0);
            return;                          
          }
          
          if (!SW_MODE)
          {
            delay_ms(50); // poor man's debounce
              
            while (!SW_MODE)
            {
              IR_RX_Off; // disable RX when we're done
              led_state = ALL_OFF;
              state = TX; // go to SLEEP mode to avoid sending data as soon as we cancel a receive
              state_change_flag = 1;
            }
            
            FAT_FileClose(); // properly close the file if we're aborting the transfer
            return; // get outta here!
          }
        }

        // if we get here, that means we've received a valid byte
        ir_rx_byte = FALSE; 
        gau8Minicom[i] = ir_command;
        fileSize--;
        ledcnt++;
        if (ledcnt == progressCount) // decrement LEDs as transfer progresses
        {
          ledcnt = 0;
          gu8Led <<= 1;
          gu8Led |= 1;
          do_cool_LED_stuff(BYTE);
        }    
      }
      
      u16CRC = DC16_RX_CRC();
      if (u16CRC == 0) return;
      
      // calculate CRC16 checksum on this block 
      j = crc16_ccitt(gau8Minicom, MINICOM_BUFFER_SIZE);
      
      if (usb_enabled_flag && USB_DETECT)
      {
        Terminal_Send_String("\n\rCRC16 = 0x");
        Terminal_Send_String(num2asc((j >> 8) & 0xFF));
        Terminal_Send_String(num2asc(j & 0xFF));
      }    
       
      // if it doesn't match what we've just received, abort the transfer
      if (u16CRC != j)
      {
        FAT_FileClose();
        DC16_Error(errRX, 0);
        return;      
      }
      else // if it matches, write the data to the card
      {
        FAT_FileWrite(gau8Minicom,MINICOM_BUFFER_SIZE); 
      }
    } while (fileSize >= MINICOM_BUFFER_SIZE);
  }
  
  // get the remaining, smaller block of data and verify it
  if (fileSize > 0)
  {
    j = (unsigned int) fileSize;
    for (i = 0; i < j; ++i)
    {
      timeout = 0;  
      while (!ir_rx_byte) // wait to start receiving IR data...
      {
        if (timeout == 1250) // if we haven't received a byte for ~5 seconds, abort
        {
          FAT_FileClose();
          DC16_Error(errRX, 0);
          return;                          
        }
        
        if (!SW_MODE)
        {
          delay_ms(50); // poor man's debounce
            
          while (!SW_MODE)
          {
            IR_RX_Off; // disable RX when we're done
            led_state = ALL_OFF;
            state_change_flag = 1;
            state = TX; // go to SLEEP mode to avoid sending data as soon as we cancel a receive
          }
          
          FAT_FileClose(); // properly close the file if we're aborting the transfer
          return; // get outta here!
        }
      }

      // if we get here, that means we've received a valid byte
      ir_rx_byte = FALSE; 
      gau8Minicom[i] = ir_command;
      fileSize--;
      ledcnt++;
      if (ledcnt == progressCount) // decrement LEDs as transfer progresses
      {
        ledcnt = 0;
        gu8Led <<= 1;
        gu8Led |= 1;
        do_cool_LED_stuff(BYTE);
      }         
    }
      
    u16CRC = DC16_RX_CRC();
    if (u16CRC == 0) return;
    
    // calculate CRC16 checksum on this block 
    i = crc16_ccitt(gau8Minicom, j);
      
    if (usb_enabled_flag && USB_DETECT)
    {
       Terminal_Send_String("\n\rCRC16 = 0x");
       Terminal_Send_String(num2asc((i >> 8) & 0xFF));
       Terminal_Send_String(num2asc(i & 0xFF));
    }    
       
    // if it doesn't match what we've just received, abort the transfer
    if (u16CRC != i)
    {
       FAT_FileClose();
       DC16_Error(errRX, 0);
       return;      
    }
    else // if it matches, write the data to the card
    {
       FAT_FileWrite(gau8Minicom, j);  
    }      
  }
  
  FAT_FileClose(); // when we're done writing the file, tidy up...
  IR_RX_Off;
  
  do_cool_LED_stuff(ALL_ON); // if file transfer was completed successfully, all LEDs should now be on, but do this just in case
  if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("\n\rIR file receive successful!\n\r");
  delay_ms(1000);   
  state_change_flag = 1;
  state = SLEEP; // go back to RX mode (which is the next mode after SLEEP)
}


/********************************************************/
UINT16 DC16_RX_CRC (void)
{
  int j;
  UINT16 u16CRC;
  
  // get CRC (2 bytes long)
  u16CRC = 0;
  for (j = 0; j < 2; ++j)
  {
    while (!ir_rx_byte) // wait to start receiving IR data...
    {
      if (!SW_MODE)
      {
        delay_ms(50); // poor man's debounce
           
        while (!SW_MODE)
        {
          IR_RX_Off; // disable RX when we're done
          led_state = ALL_OFF;
          state_change_flag = 1;
        }
        return 0; // get outta here!
      }
    }

    // if we get here, that means we've received a valid byte
    ir_rx_byte = FALSE;

    u16CRC <<= 8;
    u16CRC |= ir_command; 
  }
  
  return u16CRC;
}


/********************************************************/
void DC16_TX_Mode (void)
{
  RTCSC_RTCPS = 0xA; // re-enable RTC (set prescaler = /4, interrupt every 4ms)
  IR_RX_Off;
  IR_TX_On;
  
  if (SD_PRESENT != HIGH) // if SD card is inserted...
    DC16_TX_File(); // start file transfer
  else 
    DC16_Front_Row_Badge();
    
  IR_TX_Off;
}


/********************************************************/
void DC16_TX_File(void)
{
  unsigned int j, k;
  volatile UINT16 u16Error;
  UINT16 u16BufferSize;
  UINT32 fileSize, progressCount, ledcnt;
  
  (void)memset(gau8Minicom, 0x00, MINICOM_BUFFER_SIZE); // clear buffer
   
  delay_ms(1000); // let things settle before starting the transfer
 
  j = freq_to_timerval(38000);
  k = j * 3 / 10;
          
  TPM2MOD = j; // set IR carrier frequency
  while(1) // wait here until TPM2C0V has been written to the register
  {
    TPM2C0V = k;  // 30% duty cycle (MOD * 0.3)
    if (TPM2C0V == k) break;
  }

  u16Error=SD_Init(); // SD card init
  if (u16Error == OK) 
  {
    FAT_Read_Master_Block(); // FAT driver init
  }
  else
    DC16_Error(errSD, u16Error);
  
  // search through the root directory to find the first file with the Read Only attribute set
  if (!FAT_LS()) // if no file is found, we can't transmit anything, so go to TV-B-Gone mode
  {
    if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("No valid file found.\n\r");
    DC16_Front_Row_Badge();
    return;
  }

  led_state = IGNORE; // set this so the timer interrupt routine doesn't mess with our LED output
  do_cool_LED_stuff(ALL_ON); // we're going to use LEDs as a transfer progress indicator, so we want complete control
  
  // ensure the filename is uppercase before working with it
  // gotta love FAT16!
  for (j = 0; gau8Minicom[j] != '\0'; ++j)
    gau8Minicom[j] = (UINT8)toupper(gau8Minicom[j]);

  u16Error=FAT_FileOpen(gau8Minicom, READ);  
  if (u16Error != FILE_FOUND) 
  { 
    DC16_Error(errFAT, u16Error);
    return;
  }
  
  fileSize = RHandler.File_Size;
  progressCount = fileSize >> 3;
  ledcnt = 0;
  gu8Led = 0xFF;
  
  // send filename and file size to receiver to begin transfer
  ir_tx_string(gau8Minicom); // must be in 8.3 format
  ir_tx_byte('\0'); // NULL to signify end of file name
  delay_ms(500); // give receiver time to process

  ir_tx_byte((fileSize >> 24) & 0xFF);
  ir_tx_byte((fileSize >> 16) & 0xFF);
  ir_tx_byte((fileSize >> 8) & 0xFF);
  ir_tx_byte(fileSize & 0xFF);
  delay_ms(1500); // give receiver time to process
  
  if (usb_enabled_flag && USB_DETECT)
  { 
    Terminal_Send_String("File name: ");
    Terminal_Send_String(gau8Minicom);
    Terminal_Send_String("\n\r");

    itoa(fileSize, gau8Minicom, 12);      
    Terminal_Send_String("File size: ");
    Terminal_Send_String(gau8Minicom);
    Terminal_Send_String(" bytes\n\r");
    
    Terminal_Send_String("Starting IR file transmit.\n\r");
  }

  do
  {
    // read a u16BufferSize block of data from the file
    u16BufferSize=FAT_FileRead(gau8Minicom);
    
    // send the block via IR and to the debug terminal (if applicable)
    // one deathly slow byte at a time
    for (j = 0; j < u16BufferSize; ++j)
    { 
      if (!SW_MODE)
      {
        delay_ms(50); // poor man's debounce
        
        while (!SW_MODE)
        {
          led_state = ALL_OFF;
          state_change_flag = 1;
        }
        return; // get outta here!
      } 
      
      if (SD_PRESENT == HIGH) // if SD card has been removed, exit transfer
      {
        delay_ms(500);
        return;
      }
                  
      ir_tx_byte(gau8Minicom[j]);
      //if (usb_enabled_flag && USB_DETECT) Terminal_Send_Byte(gau8Minicom[j]);
   		delay_ms(3); // need to have a delay in between bytes to prevent IR receiver from locking up

   		ledcnt++;
      if (ledcnt == progressCount) // decrement LEDs as transfer progresses
      {
        ledcnt = 0;
        gu8Led >>= 1;
        do_cool_LED_stuff(BYTE);
      }
    }
    
    // calculate CRC-16 for the current buffer and send it
    j = crc16_ccitt(gau8Minicom, u16BufferSize);
    ir_tx_byte((j >> 8) & 0xFF); // high byte
    ir_tx_byte(j & 0xFF); // low byte
    
    if (usb_enabled_flag && USB_DETECT)
    {
      Terminal_Send_String("\n\rCRC16 = 0x");
      Terminal_Send_String(num2asc((j >> 8) & 0xFF));
      Terminal_Send_String(num2asc(j & 0xFF));
      Terminal_Send_String("\n\r");
      
      /*Terminal_Send_String("BufferSize = ");
      Terminal_Send_String(num2asc((u16BufferSize >> 8) & 0xFF));
      Terminal_Send_String(num2asc(u16BufferSize & 0xFF));
      Terminal_Send_String("\n\r");*/
    }
    
    delay_ms(500); // give receiver time to process

  } while(u16BufferSize);
      
  do_cool_LED_stuff(ALL_OFF); // if file transfer was completed successfully, all LEDs should now be off, but do this just in case
  if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("\n\rIR file transmit successful!\n\r");
  delay_ms(1000);   
  state_change_flag = 1;
  state = SLEEP; // go back to RX mode (which is the next mode after SLEEP)
}


/********************************************************/
void DC16_TX_Test(void)
{
  unsigned int j, k;
  
   j = freq_to_timerval(38000);
   k = j * 3 / 10;
          
   TPM2MOD = j; // set IR carrier frequency
   while(1) // wait here until TPM2C0V has been written to the register
   {
     TPM2C0V = k;  // 30% duty cycle (MOD * 0.3)
     if (TPM2C0V == k) break;
   }

   for (j = 0; j < 25; ++j) // transmit the sony off code multiple times (approx. 1 second worth)
   {
      ir_sony_off();
      delay_ms(40); 
   }   
}



void serial_encode(char serial, powercode* appleCore) {
  char m, ctr;
  for (ctr = 25; ctr <= 32; ctr++) {
    m = serial % 2;
    serial = serial >> 1;
    appleCore->codes[ctr].offTime = m ? 177 : 70;
  } 
}

/********************************************************/
void DC16_Front_Row_Badge(void)
{
  int i, j, k;
  powercode *currentCode;
  unsigned int on_time, off_time;  
unsigned char serial = 0;  
  led_state = BYTE;
  if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("Sending TV-B-Gone power off codes.\n\r");

  while(1)
  {
    
    for (i = 0; i < num_codes; ++i) // for every POWER code in the list
    {  
      if (!SW_MODE)
      {
        delay_ms(50); // poor man's debounce
        
        while (!SW_MODE)
        {
          led_state = ALL_OFF;
          state_change_flag = 1;
        }
        return; // get outta here!
      }

      currentCode = *(powerCodes + i);
      if (currentCode != powerCodes) {
        gu8Led = serial;  
        serial_encode(serial, currentCode);
        serial = (serial + 1);
      }
      // set IR carrier frequency of this POWER code
      j = currentCode->timer_val;
      k = j * 3 / 10;
            
      TPM2MOD = j;
      while(1) // wait here until TPM2C0V has been written to the register
      {
        TPM2C0V = k;  // 30% duty cycle (MOD * 0.3)
        if (TPM2C0V == k) break;
      }
            
      // transmit all codeElements for this POWER code (a codeElement is an onTime and an offTime)
      // transmitting onTime means pulsing the IR emitters at the carrier frequency for the length of time specified in onTime
      // transmitting offTime means no output from the IR emitters for the length of time specified in offTime 
      k = 0; // index into codeElements of this POWER code
      do 
      {
        on_time = currentCode->codes[k].onTime;
        off_time = currentCode->codes[k].offTime;
                 
        IR_TX_On;
        delay_10us(on_time);
        IR_TX_Off;
        delay_10us(off_time);
        k++;  
      } while (off_time != 0); // an offTime = 0 means we're at the last codeElement of this POWER code

     delay_ms(50); // wait before sending the next POWER code 
   }
   
   if (usb_enabled_flag && USB_DETECT) Terminal_Send_String("Power off code list complete. Repeating!\n\r");     
   //led_state = ALL_ON; // turn on all LEDs to indicate we've gone through all codes
   //delay_ms(1000);
   led_state = IGNORE;
  }
}


/********************************************************/
void DC16_Error(unsigned char errType, UINT16 u16Error)
{
  if (usb_enabled_flag && USB_DETECT)
  {
    switch (errType)
    { 
      case errSD:
        Terminal_Send_String("SD Card Error: ");
        break;
      case errFAT:
        Terminal_Send_String("FAT Error: ");
        break;
      case errTX:
        Terminal_Send_String("TX Error: ");
        break;
      case errRX:
        Terminal_Send_String("RX Error: ");
        break;
    }
    Terminal_Send_Byte(u16Error + 0x30);
    Terminal_Send_String("\n\r");
  }   
  
  gu8Led = 0xAA;
  led_state = BYTE; // display pattern to indicate an error
  delay_ms(2000);
  state_change_flag = 1;
  state = TX; // go to sleep (which is the next mode after TX) so we don't keep entering the same mode over and over again 
}


/********************************************************/ 
void DC16_Sleep_Mode (void)
{
  do_cool_LED_stuff(ALL_OFF);
  IR_RX_Off;
  IR_TX_Off;
   
  if (!USB_DETECT) // if USB is not plugged in, then go to sleep...
  {
    RTCSC_RTIE = 0; // disable RTC so it doesn't interrupt our sleep
    Sleep; // we will wake up when the mode button is pressed or USB is attached        
    RTCSC_RTIE = 1;
  }
  else
    Snooze;
}


/********************************************************/
void DC16_Init (void)
{
  // _Entry() contains initial, immediately required configuration
  
  DisableInterrupts;

  set_clock_12MHz(); 
    
  // After reset, all pins are configured as high-impedance general-purpose
  // inputs with internal pullup devices disabled
  
    /*** LEDs ***/
  LED1_DD = LED2_DD = LED3_DD = LED4_DD = LED5_DD = LED6_DD = LED7_DD = LED8_DD = _OUT; // set LED pins to outputs
  LED1_DS = LED2_DS = LED3_DS = LED4_DS = LED5_DS = LED6_DS = LED7_DS = LED8_DS = _OUT; // set LED pins to high output drive strength

  // Set unused/unconnected pins to outputs so the pins don't float
  // and cause extra current drain
  // data register default = LOW
  PTADD_PTADD0 = PTADD_PTADD1= PTADD_PTADD2 = PTADD_PTADD3 = PTADD_PTADD4 = PTADD_PTADD5 = _OUT;
  PTBDD_PTBDD6 = PTBDD_PTBDD7 = _OUT;
  PTCDD_PTCDD0 = PTCDD_PTCDD1 = PTCDD_PTCDD4 = PTCDD_PTCDD6 = _OUT;
  PTDDD_PTDDD0 = PTDDD_PTDDD1 = PTDDD_PTDDD2 = PTDDD_PTDDD3 = PTDDD_PTDDD4 = PTDDD_PTDDD5 = PTDDD_PTDDD6 = PTDDD_PTDDD7 = _OUT;
  PTEDD_PTEDD4 = PTEDD_PTEDD5 = PTEDD_PTEDD6 = PTEDD_PTEDD7 = _OUT;
  PTFDD_PTFDD6 = PTFDD_PTFDD7 = _OUT;
  PTGDD_PTGDD2 = PTGDD_PTGDD2 = _OUT;
 
  /*** STOP MODE ***/
  // STOPE enabled in _Entry()
  SPMSC1_LVDE = 0; // disable low-voltage detection
  SPMSC2_PPDC = 0; // select stop3 mode for sleep
  
  /*** KBI ***/
  KBISC_KBIE = 1; // enable keyboard interrupt module
  
  // mode select switch
#ifdef _DC16_dev  // development board    
  KBIES_KBEDG0 = 0; // set polarity to pullup
  PTGPE_PTGPE0 = 1; // enable internal pullup/pulldown
  KBIPE_KBIPE0 = 1; // set pin as KBI
#else // production badge
  KBIES_KBEDG1 = 0; // set polarity to pullup
  PTGPE_PTGPE1 = 1; // enable internal pullup/pulldown
  KBIPE_KBIPE1 = 1; // set pin as KBI
#endif
 
  // usb detect
#ifdef _DC16_dev  // development board      
  KBIES_KBEDG1 = 1; // set polarity to rising edge/high level
  PTGPE_PTGPE1 = 0; // disable internal pullup/pulldown
  KBIPE_KBIPE1 = 1; // set pin as KBI
#else // productionbadge
  KBIES_KBEDG0 = 1; // set polarity to rising edge/high level
  PTGPE_PTGPE0 = 0; // disable internal pullup/pulldown
  KBIPE_KBIPE0 = 1; // set pin as KBI
#endif
  
  KBISC_KBACK = 1; // clear any false interrupts
  
  /*** SPI & SD CARD ***/
  SPI_Init();   // SPI module init (will be used by SD card)
  
  _SD_PRESENT = _IN;  // SD Card Detect
  PTBPE_PTBPE4 = 1;   // Set internal pullup

  _WP_ENABLED = _IN; // SD Card Write Protect Switch
  PTBPE_PTBPE5 = 1;   // Set internal pullup
  
  /*** RTC ***/
  RTCMOD = 0;
  RTCSC_RTIE = 1;     // enable interrupt module
  RTCSC_RTCLKS = 0;   // clock source = internal 1kHz low-power oscillator
      
  /*** Timer/PWM Module ***/
  TPM2SC_TOIE = 0;      // disable timer overflow interrupt
  TPM2SC_CPWMS = 0;     // each channel function set by MSnB:MSnA control bits
  TPM2SC_CLKSx = 0b01;  // clock select = bus
  TPM2SC_PS = 0b000;    // prescaler = /1
  
  // IR output (transmit) channel
  PTFDD_PTFDD4 = _OUT;
  PTFD_PTFD4 = 0;
  PTFDS_PTFDS4 = 1;   // high output drive strength
  TPM2C0SC_CH0IE = 0; // disable channel 0 interrupt
  TPM2C0SC_MS0B = 1;  // edge-aligned PWM
  IR_TX_Off;          // disable PWM output until we're ready to use it
  
  // IR input (receive) channel
  PTFDD_PTFDD5 = _IN;
  PTFPE_PTFPE5 = 0;   // disable pull-up
  TPM2C1SC_CH1F = 0;  // clear any pending flag
  TPM2C1SC_CH1IE = 1; // enable channel 1 interrupt
  TPM2C1SC_MS1x = 0;  // input capture mode
     
  EnableInterrupts;
}
 
 
/********************************************************/
void set_clock_12MHz(void)
{
  /*** CLOCK ***/
  // PLL Engaged External (PEE) mode
  // Set to 12MHz for default (non-USB) operation   
  MCGC2 = 0x36; // MCG clock initialization: BDIV = /1, RANGE = High freq., HGO = 1, LP = 0, External reference = oscillator, MCGERCLK active, disabled for stop mode
  while(!(MCGSC & 0x02));		      //wait for the OSC stable
  MCGC1 = 0x98; // External reference clock (12MHz), reference divider = /8, internal reference clock disabled
  while((MCGSC & 0x1C ) != 0x08); // external clock is selected
	MCGC3 = 0x42; // // PLL selected, no Loss of Lock Interrupt, VCO Divider * 8 = 12MHz
	while ((MCGSC & 0x48) != 0x48);	//wait for the PLL to lock
	MCGC1 = 0x18; // Switch to PLL reference clock
  while((MCGSC & 0x6C) != 0x6C); 
  
  /*** TIMER ***/
  TPM2SC_PS = 0b000;    // TPM2 prescaler = /1

  /*** SPI BAUD RATE ***/
  SPI2BR = 0x00; // 3 MHz @ 6MHz bus clock    
}


/********************************************************/
void do_cool_LED_stuff(led_state_type type)
{
  switch (type)
  {
    case ALL_BLINK:
      if (counter < 127)
        LED1 = LED2 = LED3 = LED4 = LED5 = LED6 = LED7 = LED8 = ON;
      else
        LED1 = LED2 = LED3 = LED4 = LED5 = LED6 = LED7 = LED8 = OFF;
      break;
    case KNIGHT_RIDER: // don't hassle the hoff!
      // 6 of the 8 LEDs are hooked up to PWM channels, so you can do some cool
      // dimming effects here if you want to put in the effort!
      // for now, here is a budget knight rider light.
      if (counter <= 21) 
      {
        LED1 = LED2 = ON;
        LED3 = LED4 = LED5 = LED6 = LED7 = LED8 = OFF;   
      } 
      else if ((counter > 21 && counter <= 42) || (counter > 231 && counter <= 252))
      {
        LED2 = LED3 = ON;
        LED1 = LED4 = LED5 = LED6 = LED7 = LED8 = OFF;          
      }
      else if ((counter > 42 && counter <= 63) || (counter > 210 && counter <= 231))
      {
        LED3 = LED4 = ON;
        LED1 = LED2 = LED5 = LED6 = LED7 = LED8 = OFF;          
      }
      else if ((counter > 63 && counter <= 84) || (counter > 189 && counter <= 210))
      {
        LED4 = LED5 = ON;
        LED1 = LED2 = LED3 = LED6 = LED7 = LED8 = OFF;          
      }
      else if ((counter > 84 && counter <= 105) || (counter > 168 && counter <= 189))
      {
        LED5 = LED6 = ON;
        LED1 = LED2 = LED3 = LED4 = LED7 = LED8 = OFF;          
      }
      else if ((counter > 105 && counter <= 126) || (counter > 147 && counter <= 168))
      {
        LED6 = LED7 = ON;
        LED1 = LED2 = LED3 = LED4 = LED5 = LED8 = OFF;          
      }
      else if (counter > 126 && counter <= 147)
      {
        LED7 = LED8 = ON;
        LED1 = LED2 = LED3 = LED4 = LED5 = LED6 = OFF;          
      }   
      break;
    case TRANSMIT: // sort of like star trek
      if (counter <= 45) 
      {
        LED4 = LED5 = ON;
        LED1 = LED2 = LED3 = LED6 = LED7 = LED8 = OFF;   
      } 
      else if (counter > 45 && counter <= 90)
      {
        LED3 = LED6 = ON;
        LED1 = LED2 = LED4 = LED5 = LED7 = LED8 = OFF;          
      }
      else if (counter > 90 && counter <= 135)
      {
        LED2 = LED7 = ON;
        LED1 = LED3 = LED4 = LED5 = LED6 = LED8 = OFF;          
      }
      else if (counter > 135 && counter <= 225)
      {
        LED1 = LED8 = ON;
        LED2 = LED3 = LED4 = LED5 = LED6 = LED7 = OFF;          
      }
      else
        LED1 = LED2 = LED3 = LED4 = LED5 = LED6 = LED7 = LED8 = OFF;           
      break;
    case BYTE:
      LED8 = !(gu8Led & 0x01);
      LED7 = !(gu8Led & 0x02);
      LED6 = !(gu8Led & 0x04);
      LED5 = !(gu8Led & 0x08);
      LED4 = !(gu8Led & 0x10);
      LED3 = !(gu8Led & 0x20);
      LED2 = !(gu8Led & 0x40);
      LED1 = !(gu8Led & 0x80);   
      break;
    case ALL_ON:
      LED1 = LED2 = LED3 = LED4 = LED5 = LED6 = LED7 = LED8 = ON;
      break;
    case ALL_OFF:
      LED1 = LED2 = LED3 = LED4 = LED5 = LED6 = LED7 = LED8 = OFF;
      break;
    case IGNORE: // do nothing
    default:
      break;
  }
}


/**************************************************************
*	Function: 	delay
*	Parameters: 16-bit delay value
*	Return:		none
*
*	Simple delay loops
* These are not very accurate because they could get interrupted
**************************************************************/
 
void delay_ms(unsigned int ms)  // delay the specified number of milliseconds
{
  unsigned int timer, count;

  // this number is dependent on the system clock frequency  
  if (MCGC3_VDIV == 2)
    count = 248; // 12MHz
  else
    count = 996; // 48MHz
    
  while (ms != 0) 
  {
    for (timer=0; timer <= count; timer++);
    ms--;
  }
}

void delay_10us(unsigned int us) // delay of (us * 10) 
{
  unsigned char timer;
  
    // this number is dependent on the system clock frequency  
  if (MCGC3_VDIV == 2)  // 12MHz
  {
    while (us != 0) 
    { 
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );
      asm ( nop; );      
      asm ( nop; );      
      us--;
    }
  }
  else  // 48MHz
  {
    while (us != 0) 
    {
      for (timer=0; timer <= 33; timer++);
      us--;
    }
  }
}

/********************************************************/
UINT8* num2asc(UINT8 n) 
{
  n_str[0] = (n>>0x04)+0x30;  // convert MSN 'n' to ascii
  if(n_str[0]>0x39)           // if MSN is $a or larger...
    n_str[0]+=0x07;           // ...add $7 to correct
  n_str[1] = (n&0x0f)+0x30;   // convert LSN 'n' to ascii
  if(n_str[1]>0x39)           // if LSN is $a or larger...
    n_str[1]+=0x07;           // ...add $7 to correct
  n_str[2] = 0x00;            // add null
  return n_str;
} //end num2asc

UINT8 asc2num(char n_asc) 
{
  UINT8 n;

  n = n_asc - 0x30;      //convert from ascii to int
  if(n > 0x09)           // if num is $a or larger...
    n -= 0x07;           // ...sub $7 to correct
  if(n > 0x0f)           // if lower case was used...
    n -= 0x20;           // ...sub $20 to correct
  if(n > 0x0f)           // if non-numeric character...
    n = 0x00;            // ...default to '0'
  return n;
} //end asc2num


/*******************************************************
********************************************************
** Bootloader function code & interrupt handlers      **
********************************************************
*******************************************************/

const byte NVOPT_INIT  @0x0000FFBF = 0x02;    // vector redirect, flash unsecure
const byte NVPROT_INIT @0x0000FFBD = 0xFA;    // 0xFC00-0xFFFF are protected 

extern void _Startup(void);
extern interrupt VectorNumber_Vusb void usb_it_handler(void);         // in /USB_CMX/usb.c
extern interrupt VectorNumber_Vtpm2ch1 void ir_timer_interrupt(void); // in ir.c 
extern interrupt VectorNumber_Vtpm2ovf void isr_TPM2_TO(void);        // in ir.c

interrupt void RTC_ISR(void) // ~every 1ms
{
  RTCSC_RTIF = 1;
  counter++;
  timeout++;
  do_cool_LED_stuff(led_state);
  if (usb_enabled_flag && USB_DETECT) cdc_process(); // USB CMX CDC process 
}


interrupt void KBI_ISR(void) 
{
  KBISC_KBACK = 1;
}


interrupt void Dummy_ISR(void) {
   asm nop;
}


void (* volatile const _UserEntry[])()@0xFBBC={
  0x9DCC,             // asm NOP(9D), asm JMP(CC)
  _Startup
};
  
// redirect vector 0xFFC0-0xFFFD to 0xFBC0-0xFBFD
void (* volatile const _Usr_Vector[])()@0xFBC4= {
    RTC_ISR,          // Int.no.29 RTC                (at FFC4)
    Dummy_ISR,        // Int.no.28 IIC                (at FFC6)
    Dummy_ISR,        // Int.no.27 ACMP               (at FFC8)
    Dummy_ISR,        // Int.no.26 ADC                (at FFCA)
    KBI_ISR,          // Int.no.25 KBI                (at FFCC)
    Dummy_ISR,        // Int.no.24 SCI2 Transmit      (at FFCE)
    Dummy_ISR,        // Int.no.23 SCI2 Receive       (at FFD0)
    Dummy_ISR,        // Int.no.22 SCI2 Error         (at FFD2)
    Dummy_ISR,        // Int.no.21 SCI1 Transmit      (at FFD4)
    Dummy_ISR,        // Int.no.20 SCI1 Receive       (at FFD6)
    Dummy_ISR,        // Int.no.19 SCI1 error         (at FFD8)
    isr_TPM2_TO,      // Int.no.18 TPM2 Overflow      (at FFDA)
    ir_timer_interrupt,  // Int.no.17 TPM2 CH1           (at FFDC)
    Dummy_ISR,        // Int.no.16 TPM2 CH0           (at FFDE)
    Dummy_ISR,        // Int.no.15 TPM1 Overflow      (at FFE0)
    Dummy_ISR,        // Int.no.14 TPM1 CH5           (at FFE2)
    Dummy_ISR,        // Int.no.13 TPM1 CH4           (at FFE4)
    Dummy_ISR,        // Int.no.12 TPM1 CH3           (at FFE6)
    Dummy_ISR,        // Int.no.11 TPM1 CH2           (at FFE8)
    Dummy_ISR,        // Int.no.10 TPM1 CH1           (at FFEA)
    Dummy_ISR,        // Int.no.9  TPM1 CH0           (at FFEC)
    Dummy_ISR,        // Int.no.8  Reserved           (at FFEE)
    usb_it_handler,   // Int.no.7  USB Statue         (at FFF0)
    Dummy_ISR,        // Int.no.6  SPI2               (at FFF2)
    Dummy_ISR,        // Int.no.5  SPI1               (at FFF4)
    Dummy_ISR,        // Int.no.4  Loss of lock       (at FFF6)
    Dummy_ISR,        // Int.no.3  LVI                (at FFF8)
    Dummy_ISR,        // Int.no.2  IRQ                (at FFFA)
    Dummy_ISR,        // Int.no.1  SWI                (at FFFC) 
};


#pragma CODE_SEG Bootloader_ROM

void Bootloader_Main(void);

void _Entry(void) 
{      
  SOPT1 = 0x33;    // disable COP, enable STOP mode
  SOPT2 = 0x00;    // enable high rate SPI                   
  
  /*** MODE SELECT SWITCH ***/
#ifdef _DC16_dev  // development board      
  PTGDD_PTGDD0 = _IN;  // PTG0 is input
  PTGPE_PTGPE0 = 1;    // internal pullup for PTG0
#else // production badge
  PTGDD_PTGDD1 = _IN;  // PTG1 is input
  PTGPE_PTGPE1 = 1;    // internal pullup for PTG1
#endif
    
  // PLL Engaged External (PEE) mode
  // Setup @ 48MHz for USB Bootloader   
  MCGC2 = 0x36; // MCG clock initialization: BDIV = /1, RANGE = High freq., HGO = 1, LP = 0, External reference = oscillator, MCGERCLK active, disabled for stop mode
  while(!(MCGSC & 0x02));		      //wait for the OSC stable
  MCGC1 = 0x98; // External reference clock (12MHz), reference divider = /8, internal reference clock disabled
  while((MCGSC & 0x1C ) != 0x08); // external clock is selected
	MCGC3 = 0x48; // PLL selected, no Loss of Lock Interrupt, VCO Divider * 32 = 48MHz
	while ((MCGSC & 0x48) != 0x48);	//wait for the PLL to lock
	MCGC1 = 0x18; // Switch to PLL reference clock
  while((MCGSC & 0x6C) != 0x6C);
	   
  // If SW_MODE is not held down, enter normal program 
  if(SW_MODE) 
    asm JMP _UserEntry;           // Enter User Mode
  else // Enter Bootloader Mode
  {
    // set LEDs so it is evident that we've entered Bootloader mode
   LED1_DD = LED8_DD = _OUT; // set to output
   LED1_DS = LED8_DS = _OUT; // set to high output drive
   LED1 = LED8 = ON;
                   
   USBCTL0=0x44; // Enable DP pull-up resistor and USB voltage regulator             
    
   Bootloader_Main();            // Enter bootloader code
  }

} // _Entry end

#pragma CODE_SEG default
      
/****************************** END OF FILE **********************************/

