/*
 * Aeon - Open Source Reconfigurable Computer
 * Copyright (C) 2013-2015 Dmitriy Schapotschkin (ilovespeccy@speccyland.net)
 * Project Homepage: http://www.speccyland.net
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <usb/usb.h>
#include <usb/usb_device.h>
#include <usb/usb_device_cdc.h>
#include <usb/usb_device_generic.h>
#include "dataflash.h"
#include "usb_handler.h"
#include "fpga.h"
#include "rtc.h"

volatile USB_HANDLE USBGenericOutHandle;
volatile USB_HANDLE USBGenericInHandle;

unsigned char USBGenericOutPacket[64];
unsigned char USBGenericInPacket[64];

// Local Functions
void USB_Generic_Handler(void);
void USB_Generic_Flush(void);

void USB_Init(void)
{
    USBDeviceInit();
    USBDeviceAttach();
}

void USB_Handler(void)
{
   #if defined(USB_POLLING)
      USBDeviceTasks();
   #endif

   if (USBGetDeviceState() < CONFIGURED_STATE)
      return;

   if( USBIsDeviceSuspended() == true )
      return;

   USB_Generic_Handler();
}

bool USER_USB_CALLBACK_EVENT_HANDLER(USB_EVENT event, void *pdata, uint16_t size)
{
    switch((int)event)
    {
        case EVENT_CONFIGURED:
            USBGenericInHandle = 0;
            USBEnableEndpoint(USBGEN_EP_NUM, USB_OUT_ENABLED|USB_IN_ENABLED|USB_HANDSHAKE_ENABLED|USB_DISALLOW_SETUP);
            USBGenericOutHandle = USBGenRead(USBGEN_EP_NUM, (uint8_t*)&USBGenericOutPacket, USBGEN_EP_SIZE);
            break;

        case EVENT_EP0_REQUEST:
            USBCheckCDCRequest();
            break;

         default:
            break;
    }
    return true;
}

#if defined(USB_INTERRUPT)
void __attribute__((interrupt,auto_psv)) _USB1Interrupt()
{
    USBDeviceTasks();
}
#endif

void USB_Generic_Handler(void)
{
   if(!USBHandleBusy(USBGenericOutHandle)) // data recieved from host
   {
      UserLED1_On;
      switch (USBGenericOutPacket[0])
      {
         case CMD_FPGA_GET_STATUS:        // Get FPGA Status; Byte 0 - INIT_B, Byte 1 - DONE
         {
            USBGenericInPacket[0] = INIT_B;
            USBGenericInPacket[1] = CONF_DONE;
            USB_Generic_Flush();
            break;
         }

         case CMD_FPGA_RESET:             // Reset FPGA
         {
            FPGA_Reset();
            break;
         }

         case CMD_FPGA_WRITE_BITSTREAM:   // Configure FPGA (Bytes 1-63)
         {
            UserLED2_On;
            FPGA_Write_Bitstream(&USBGenericOutPacket[1], 63);
            UserLED2_Off;
            break;
         }

         case CMD_RTC_WRITE:
         {
            RTC_Write(USBGenericOutPacket[1], (uint8_t*)&USBGenericOutPacket[3], USBGenericOutPacket[2]);
            break;
         }

         case CMD_RTC_READ:
         {
            RTC_Read(USBGenericOutPacket[1], (uint8_t*)&USBGenericInPacket, USBGenericOutPacket[2]);
            USB_Generic_Flush();
            break;
         }

         // ==================================================================================================================
         case CMD_SET_SPI_MASTER:
         {
            SPI_Mux = USBGenericOutPacket[1];
            break;
         }

         case CMD_DATAFLASH_RESET:
         {
            DataFlash_SoftReset();
            break;
         }

         case CMD_DATAFLASH_POWER_OF_TWO:
         {
            DataFlash_PowerOfTwo();
            break;
         }

         case CMD_DATAFLASH_GET_STATUS:
         {
            USBGenericInPacket[0] = DataFlash_GetStatus();
            USB_Generic_Flush();
            break;
         }

         case CMD_DATAFLASH_CHIP_ERASE:
         {
            DataFlash_ChipErase(0);
            break;
         }

         case CMD_DATAFLASH_FILL_BUFFER:
         {
            unsigned short pos = (((unsigned short)USBGenericOutPacket[1] << 8) | USBGenericOutPacket[2]) & 0x1FF;
            DataFlash_FillBuffer(pos, &USBGenericOutPacket[3], 32);
            break;
         }

         case CMD_DATAFLASH_FLUSH_BUFFER:
         {
            unsigned short page = ((unsigned short)USBGenericOutPacket[1] << 8) | USBGenericOutPacket[2];
            DataFlash_FlushBuffer(page);
            break;
         }

         case CMD_DATAFLASH_READ:
         {
            unsigned long address = ((unsigned long)USBGenericOutPacket[1] << 16) |
                                    ((unsigned long)USBGenericOutPacket[2] << 8)  |
                                                    USBGenericOutPacket[3];
            DataFlash_ReadBlock(address, USBGenericInPacket, 32);
            USB_Generic_Flush();
            break;
         }

         case CMD_SRAM_CONFIGURE:
         {
            unsigned long address = ((unsigned long)USBGenericOutPacket[1] << 16) |
                                    ((unsigned long)USBGenericOutPacket[2] << 8)  |
                                                    USBGenericOutPacket[3];
            RAMSetAddr(address, USBGenericOutPacket[4]);
            break;
         }

         case CMD_SRAM_WRITE:
         {
            UserLED2_On;
            WriteRAMFromBuffer(&USBGenericOutPacket[2], USBGenericOutPacket[1]);
            UserLED2_Off;
            break;
         }

         case CMD_SRAM_READ:
         {
            UserLED2_On;
            ReadRAMToBuffer(USBGenericInPacket, USBGenericOutPacket[1]);
            USB_Generic_Flush();
            UserLED2_Off;
            break;
         }
         // ==================================================================================================================
      }
      UserLED1_Off;
      USBGenericOutHandle = USBGenRead(USBGEN_EP_NUM,(uint8_t*)&USBGenericOutPacket,USBGEN_EP_SIZE); // Refresh USB
   }
}

void USB_Generic_Flush(void)
{
   USBGenericInHandle = USBGenWrite(USBGEN_EP_NUM, (uint8_t*)&USBGenericInPacket, USBGEN_EP_SIZE);
   while (USBHandleBusy(USBGenericInHandle));
}
