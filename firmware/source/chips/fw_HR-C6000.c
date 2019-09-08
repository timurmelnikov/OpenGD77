/*
 * Copyright (C)2019 Kai Ludwig, DG4KLU
 *
 * Additional code by Roger Clark VK3KYY / G4KYF
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "fw_HR-C6000.h"
#include "menu/menuUtilityQSOData.h"
#include "fw_settings.h"
#include "menu/menuHotspot.h"

#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
#include <SeggerRTT/RTT/SEGGER_RTT.h>
#endif

TaskHandle_t fwhrc6000TaskHandle;

const uint8_t TG_CALL_FLAG = 0x00;
const uint8_t PC_CALL_FLAG = 0x03;
const uint8_t SILENCE_AUDIO[27] = {	0xB9U, 0xE8U, 0x81U, 0x52U, 0x61U, 0x73U, 0x00U, 0x2AU, 0x6BU, 0xB9U, 0xE8U, 0x81U, 0x52U,
									0x61U, 0x73U, 0x00U, 0x2AU, 0x6BU, 0xB9U, 0xE8U, 0x81U, 0x52U, 0x61U, 0x73U, 0x00U, 0x2AU, 0x6BU };

uint8_t tmp_val_0x82;
uint8_t tmp_val_0x86;
uint8_t tmp_val_0x51;
uint8_t tmp_val_0x52;
uint8_t tmp_val_0x57;
uint8_t tmp_val_0x5f;
uint8_t DMR_frame_buffer[256];

volatile bool int_sys;
volatile bool int_ts;
volatile bool tx_required;
volatile bool int_rxtx;
volatile int int_timeout;



static uint32_t receivedTgOrPcId;
static uint32_t receivedSrcId;
int slot_state;
int tick_cnt;
int skip_count;
int tx_sequence;
uint8_t spi_tx[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void SPI_HR_C6000_init()
{
    // C6000 interrupts
    PORT_SetPinMux(Port_INT_C6000_RF_RX, Pin_INT_C6000_RF_RX, kPORT_MuxAsGpio);
    PORT_SetPinMux(Port_INT_C6000_RF_TX, Pin_INT_C6000_RF_TX, kPORT_MuxAsGpio);
    PORT_SetPinMux(Port_INT_C6000_SYS, Pin_INT_C6000_SYS, kPORT_MuxAsGpio);
    PORT_SetPinMux(Port_INT_C6000_TS, Pin_INT_C6000_TS, kPORT_MuxAsGpio);
    GPIO_PinInit(GPIO_INT_C6000_RF_RX, Pin_INT_C6000_RF_RX, &pin_config_input);
    GPIO_PinInit(GPIO_INT_C6000_RF_TX, Pin_INT_C6000_RF_TX, &pin_config_input);
    GPIO_PinInit(GPIO_INT_C6000_SYS, Pin_INT_C6000_SYS, &pin_config_input);
    GPIO_PinInit(GPIO_INT_C6000_TS, Pin_INT_C6000_TS, &pin_config_input);

    // Connections with C6000
    PORT_SetPinMux(Port_INT_C6000_RESET, Pin_INT_C6000_RESET, kPORT_MuxAsGpio);
    PORT_SetPinMux(Port_INT_C6000_PWD, Pin_INT_C6000_PWD, kPORT_MuxAsGpio);
    GPIO_PinInit(GPIO_INT_C6000_RESET, Pin_INT_C6000_RESET, &pin_config_output);
    GPIO_PinInit(GPIO_INT_C6000_PWD, Pin_INT_C6000_PWD, &pin_config_output);
    GPIO_PinWrite(GPIO_INT_C6000_RESET, Pin_INT_C6000_RESET, 1);
    GPIO_PinWrite(GPIO_INT_C6000_PWD, Pin_INT_C6000_PWD, 1);

    // Wake up C6000
	vTaskDelay(portTICK_PERIOD_MS * 10);
    GPIO_PinWrite(GPIO_INT_C6000_PWD, Pin_INT_C6000_PWD, 0);
	vTaskDelay(portTICK_PERIOD_MS * 10);

	// GD-77 FW V3.1.1 data from 0x76010 / length 0x06
	uint8_t spi_init_values_1[] = { 0xd5, 0xd7, 0xf7, 0x7f, 0xd7, 0x57 };
	// GD-77 FW V3.1.1 data from 0x75F70 / length 0x20
	uint8_t spi_init_values_2[] = { 0x69, 0x69, 0x96, 0x96, 0x96, 0x99, 0x99, 0x99, 0xa5, 0xa5, 0xaa, 0xaa, 0xcc, 0xcc, 0x00, 0xf0, 0x01, 0xff, 0x01, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x10, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	// GD-77 FW V3.1.1 data from 0x75F90 / length 0x10
	uint8_t spi_init_values_3[] = { 0x00, 0x00, 0x14, 0x1e, 0x1a, 0xff, 0x3d, 0x50, 0x07, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	// GD-77 FW V3.1.1 data from 0x75FA0 / length 0x07
	uint8_t spi_init_values_4[] = { 0x00, 0x03, 0x01, 0x02, 0x05, 0x1e, 0xf0 };
	// GD-77 FW V3.1.1 data from 0x75FA8 / length 0x05
	uint8_t spi_init_values_5[] = { 0x00, 0x00, 0xeb, 0x78, 0x67 };
	// GD-77 FW V3.1.1 data from 0x75FB0 / length 0x60
	uint8_t spi_init_values_6[] = { 0x32, 0xef, 0x00, 0x31, 0xef, 0x00, 0x12, 0xef, 0x00, 0x13, 0xef, 0x00, 0x14, 0xef, 0x00, 0x15, 0xef, 0x00, 0x16, 0xef, 0x00, 0x17, 0xef, 0x00, 0x18, 0xef, 0x00, 0x19, 0xef, 0x00, 0x1a, 0xef, 0x00, 0x1b, 0xef, 0x00, 0x1c, 0xef, 0x00, 0x1d, 0xef, 0x00, 0x1e, 0xef, 0x00, 0x1f, 0xef, 0x00, 0x20, 0xef, 0x00, 0x21, 0xef, 0x00, 0x22, 0xef, 0x00, 0x23, 0xef, 0x00, 0x24, 0xef, 0x00, 0x25, 0xef, 0x00, 0x26, 0xef, 0x00, 0x27, 0xef, 0x00, 0x28, 0xef, 0x00, 0x29, 0xef, 0x00, 0x2a, 0xef, 0x00, 0x2b, 0xef, 0x00, 0x2c, 0xef, 0x00, 0x2d, 0xef, 0x00, 0x2e, 0xef, 0x00, 0x2f, 0xef, 0x00 };

	// --- start spi_init_daten_senden()
	write_SPI_page_reg_byte_SPI0(0x04, 0x0b, 0x40);
	write_SPI_page_reg_byte_SPI0(0x04, 0x0c, 0x32);
	write_SPI_page_reg_byte_SPI0(0x04, 0xb9, 0x05);
	write_SPI_page_reg_byte_SPI0(0x04, 0x0a, 0x01);

	write_SPI_page_reg_bytearray_SPI0(0x01, 0x04, spi_init_values_1, 0x06);
	write_SPI_page_reg_bytearray_SPI0(0x01, 0x10, spi_init_values_2, 0x20);
	write_SPI_page_reg_bytearray_SPI0(0x01, 0x30, spi_init_values_3, 0x10);
	write_SPI_page_reg_bytearray_SPI0(0x01, 0x40, spi_init_values_4, 0x07);
	write_SPI_page_reg_bytearray_SPI0(0x01, 0x51, spi_init_values_5, 0x05);
	write_SPI_page_reg_bytearray_SPI0(0x01, 0x60, spi_init_values_6, 0x60);

	write_SPI_page_reg_byte_SPI0(0x04, 0x00, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x10, 0x6E); // (oder 0x02 oder 0x6A)
	write_SPI_page_reg_byte_SPI0(0x04, 0x11, 0x80);
	write_SPI_page_reg_byte_SPI0(0x04, 0x13, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x1F, 0x10); // DMR Colour code in upper 4 bits - defaulted to 1, and is updated elsewhere in the code
	write_SPI_page_reg_byte_SPI0(0x04, 0x20, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x21, 0xA0);
	write_SPI_page_reg_byte_SPI0(0x04, 0x22, 0x26);
	write_SPI_page_reg_byte_SPI0(0x04, 0x22, 0x86);
	write_SPI_page_reg_byte_SPI0(0x04, 0x25, 0x0E);
	write_SPI_page_reg_byte_SPI0(0x04, 0x26, 0x7D);
	write_SPI_page_reg_byte_SPI0(0x04, 0x27, 0x40);
	write_SPI_page_reg_byte_SPI0(0x04, 0x28, 0x7D);
	write_SPI_page_reg_byte_SPI0(0x04, 0x29, 0x40);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2A, 0x0B);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2B, 0x0B);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2C, 0x17);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2D, 0x05);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2E, 0x04);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2F, 0x0B);
	write_SPI_page_reg_byte_SPI0(0x04, 0x32, 0x02);
	write_SPI_page_reg_byte_SPI0(0x04, 0x33, 0xFF);
	write_SPI_page_reg_byte_SPI0(0x04, 0x34, 0xF0);
	write_SPI_page_reg_byte_SPI0(0x04, 0x35, 0x28);
	write_SPI_page_reg_byte_SPI0(0x04, 0x3E, 0x28);
	write_SPI_page_reg_byte_SPI0(0x04, 0x3F, 0x10);
	write_SPI_page_reg_byte_SPI0(0x04, 0x36, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x37, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x4B, 0x1B);
	write_SPI_page_reg_byte_SPI0(0x04, 0x4C, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x56, 0x00); // / (dynamisch)
	write_SPI_page_reg_byte_SPI0(0x04, 0x5F, 0xC0); // | (dynamisch)
	write_SPI_page_reg_byte_SPI0(0x04, 0x81, 0xFF); // \ (dynamisch)
	write_SPI_page_reg_byte_SPI0(0x04, 0xD1, 0xC4);

	// --- start subroutine spi_init_daten_senden_sub()
	write_SPI_page_reg_byte_SPI0(0x04, 0x01, 0x70); // (dynamisch)
	write_SPI_page_reg_byte_SPI0(0x04, 0x03, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x05, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x12, 0x15); // (dynamisch)
	write_SPI_page_reg_byte_SPI0(0x04, 0xA1, 0x80); // (dynamisch)
	write_SPI_page_reg_byte_SPI0(0x04, 0xC0, 0x0A);
	write_SPI_page_reg_byte_SPI0(0x04, 0x06, 0x21);
	write_SPI_page_reg_byte_SPI0(0x04, 0x07, 0x0B);
	write_SPI_page_reg_byte_SPI0(0x04, 0x08, 0xB8);
	write_SPI_page_reg_byte_SPI0(0x04, 0x09, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x0D, 0x10);
	write_SPI_page_reg_byte_SPI0(0x04, 0x0E, 0x8E);
	write_SPI_page_reg_byte_SPI0(0x04, 0x0F, 0xB8);
	write_SPI_page_reg_byte_SPI0(0x04, 0xC2, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE0, 0x8B);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE1, 0x0F);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE2, 0x06);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE3, 0x52);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE4, 0x4A);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE5, 0x1A);
	// --- end subroutine spi_init_daten_senden_sub()

	write_SPI_page_reg_byte_SPI0(0x04, 0x40, 0xC3); // / (dynamisch)
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x40); // \ (dynamisch)
	// --- end spi_init_daten_senden()

	// ------ start spi_more_init
	// --- start sub_1B5A4
	set_clear_SPI_page_reg_byte_with_mask_SPI0(0x04, 0x06, 0xFD, 0x02); // SET
	// --- end sub_1B5A4

	// --- start sub_1B5DC
	// hard coded 128 * 0xAA
	uint8_t spi_values[128];
	for (int i=0; i<128;i++)
	{
		spi_values[i]=0xaa;
	}
	write_SPI_page_reg_bytearray_SPI0(0x03, 0x00, spi_values, 0x80);
	// --- end sub_1B5DC

	// --- start sub_1B5A4
	set_clear_SPI_page_reg_byte_with_mask_SPI0(0x04, 0x06, 0xFD, 0x00); // CLEAR
	// --- end sub_1B5A4

	write_SPI_page_reg_byte_SPI0(0x04, 0x37, 0x9E); // (dynamisch)
	set_clear_SPI_page_reg_byte_with_mask_SPI0(0x04, 0xE4, 0x3F, 0x00); // CLEAR
	// ------ end spi_more_init
}

void SPI_C6000_postinit()
{
	write_SPI_page_reg_byte_SPI0(0x04, 0x04, 0xE8);
	write_SPI_page_reg_byte_SPI0(0x04, 0x46, 0x37);
	write_SPI_page_reg_byte_SPI0(0x04, 0x48, 0x03);
	write_SPI_page_reg_byte_SPI0(0x04, 0x47, 0xE8);
	// GD-77 FW V3.1.1 data from 0x76010 / length 0x06
	uint8_t spi_init_values_1[] = { 0xd5, 0xd7, 0xf7, 0x7f, 0xd7, 0x57 };
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x20);
	write_SPI_page_reg_byte_SPI0(0x04, 0x40, 0x03);
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x00, 0x3F);
	write_SPI_page_reg_bytearray_SPI0(0x01, 0x04, spi_init_values_1, 0x06);
	write_SPI_page_reg_byte_SPI0(0x04, 0x10, 0x6E);
	write_SPI_page_reg_byte_SPI0(0x04, 0x1F, 0x10); // DMR Colour code in upper 4 bits - defaulted to 1, and is updated elsewhere in the code
	write_SPI_page_reg_byte_SPI0(0x04, 0x26, 0x7D);
	write_SPI_page_reg_byte_SPI0(0x04, 0x27, 0x40);
	write_SPI_page_reg_byte_SPI0(0x04, 0x28, 0x7D);
	write_SPI_page_reg_byte_SPI0(0x04, 0x29, 0x40);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2A, 0x0B);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2B, 0x0B);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2C, 0x17);
	write_SPI_page_reg_byte_SPI0(0x04, 0x2D, 0x05);
	write_SPI_page_reg_byte_SPI0(0x04, 0x56, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x5F, 0xC0);
	write_SPI_page_reg_byte_SPI0(0x04, 0x81, 0xFF);
	write_SPI_page_reg_byte_SPI0(0x04, 0x01, 0x70);
	write_SPI_page_reg_byte_SPI0(0x04, 0x03, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x05, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x12, 0x15);
	write_SPI_page_reg_byte_SPI0(0x04, 0xA1, 0x80);
	write_SPI_page_reg_byte_SPI0(0x04, 0xC0, 0x0A);
	write_SPI_page_reg_byte_SPI0(0x04, 0x06, 0x21);
	write_SPI_page_reg_byte_SPI0(0x04, 0x07, 0x0B);
	write_SPI_page_reg_byte_SPI0(0x04, 0x08, 0xB8);
	write_SPI_page_reg_byte_SPI0(0x04, 0x09, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x0D, 0x10);
	write_SPI_page_reg_byte_SPI0(0x04, 0x0E, 0x8E);
	write_SPI_page_reg_byte_SPI0(0x04, 0x0F, 0xB8);
	write_SPI_page_reg_byte_SPI0(0x04, 0xC2, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE0, 0x8B);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE1, 0x0F);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE2, 0x06);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE3, 0x52);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE4, 0x4A);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE5, 0x1A);
	write_SPI_page_reg_byte_SPI0(0x04, 0x26, 0x7D);
	write_SPI_page_reg_byte_SPI0(0x04, 0x27, 0x40);
	write_SPI_page_reg_byte_SPI0(0x04, 0x28, 0x7D);
	write_SPI_page_reg_byte_SPI0(0x04, 0x29, 0x40);
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x20);
	write_SPI_page_reg_byte_SPI0(0x04, 0x40, 0xC3);
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x40);
	write_SPI_page_reg_byte_SPI0(0x04, 0x01, 0x70);
	write_SPI_page_reg_byte_SPI0(0x04, 0x10, 0x6E);
	write_SPI_page_reg_byte_SPI0(0x04, 0x00, 0x3F);
	write_SPI_page_reg_byte_SPI0(0x04, 0xE4, 0x4B);
}

void PORTC_IRQHandler(void)
{
    if ((1U << Pin_INT_C6000_SYS) & PORT_GetPinsInterruptFlags(Port_INT_C6000_SYS))
    {
    	int_sys=true;
    	timer_hrc6000task=0;
        PORT_ClearPinsInterruptFlags(Port_INT_C6000_SYS, (1U << Pin_INT_C6000_SYS));
    }
    if ((1U << Pin_INT_C6000_TS) & PORT_GetPinsInterruptFlags(Port_INT_C6000_TS))
    {
    	int_ts=true;
    	timer_hrc6000task=0;
        PORT_ClearPinsInterruptFlags(Port_INT_C6000_TS, (1U << Pin_INT_C6000_TS));
    }
    if ((1U << Pin_INT_C6000_RF_RX) & PORT_GetPinsInterruptFlags(Port_INT_C6000_RF_RX))
    {
    	tx_required=false;
    	int_rxtx=true;
        PORT_ClearPinsInterruptFlags(Port_INT_C6000_RF_RX, (1U << Pin_INT_C6000_RF_RX));
    }
    if ((1U << Pin_INT_C6000_RF_TX) & PORT_GetPinsInterruptFlags(Port_INT_C6000_RF_TX))
    {
    	tx_required=true;
    	int_rxtx=true;
        PORT_ClearPinsInterruptFlags(Port_INT_C6000_RF_TX, (1U << Pin_INT_C6000_RF_TX));
    }

    int_timeout=0;

    /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
    exception return operation might vector to incorrect interrupt */
    __DSB();
}

void init_HR_C6000_interrupts()
{
	init_digital_state();

    PORT_SetPinInterruptConfig(Port_INT_C6000_SYS, Pin_INT_C6000_SYS, kPORT_InterruptEitherEdge);
    PORT_SetPinInterruptConfig(Port_INT_C6000_TS, Pin_INT_C6000_TS, kPORT_InterruptEitherEdge);
    PORT_SetPinInterruptConfig(Port_INT_C6000_RF_RX, Pin_INT_C6000_RF_RX, kPORT_InterruptEitherEdge);
    PORT_SetPinInterruptConfig(Port_INT_C6000_RF_TX, Pin_INT_C6000_RF_TX, kPORT_InterruptEitherEdge);

    NVIC_SetPriority(PORTC_IRQn, 3);
}

void init_digital_state()
{
	taskENTER_CRITICAL();
	int_sys=false;
	int_ts=false;
	tx_required=false;
	int_rxtx=false;
	int_timeout=0;
	taskEXIT_CRITICAL();
	slot_state = DMR_STATE_IDLE;
	tick_cnt=0;
	skip_count=0;
	qsodata_timer = 0;
}

void init_digital_DMR_RX()
{
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x20);
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x00);
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x20);
	write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x40);
}

void init_digital()
{
	GPIO_PinWrite(GPIO_speaker_mute, Pin_speaker_mute, 0);
    GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 0);
	init_digital_DMR_RX();
	init_digital_state();
    NVIC_EnableIRQ(PORTC_IRQn);
	init_codec();
}

void terminate_digital()
{
	GPIO_PinWrite(GPIO_speaker_mute, Pin_speaker_mute, 0);
    GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 0);
	init_digital_state();
    NVIC_DisableIRQ(PORTC_IRQn);
}

void store_qsodata()
{
	// If this is the start of a newly received signal, we always need to trigger the display to show this, even if its the same station calling again.
	// Of if the display is holding on the PC accept text and the incoming call is not a PC
	if (qsodata_timer == 0 || (menuUtilityReceivedPcId!=0 && DMR_frame_buffer[0]==TG_CALL_FLAG))
	{
		menuDisplayQSODataState = QSO_DISPLAY_CALLER_DATA;
	}
	// check if this is a valid data frame, including Talker Alias data frames (0x04 - 0x07)
	// Not sure if its necessary to check byte [1] for 0x00 but I'm doing this
	if (DMR_frame_buffer[1] == 0x00  && (DMR_frame_buffer[0]==TG_CALL_FLAG || DMR_frame_buffer[0]==PC_CALL_FLAG  || (DMR_frame_buffer[0]>=0x04 && DMR_frame_buffer[0]<=0x7)))
	{
		// Needs to enter critical because the LastHeard is accessed by other threads
    	taskENTER_CRITICAL();
    	lastHeardListUpdate(DMR_frame_buffer);
		taskEXIT_CRITICAL();
		qsodata_timer=QSO_TIMER_TIMEOUT;
	}
}

void init_hrc6000_task()
{
	xTaskCreate(fw_hrc6000_task,                        /* pointer to the task */
				"fw hrc6000 task",                      /* task name for kernel awareness debugging */
				5000L / sizeof(portSTACK_TYPE),      /* task stack size */
				NULL,                      			 /* optional task startup argument */
				5U,                                  /* initial priority */
				fwhrc6000TaskHandle					 /* optional task handle to create */
				);
}

void fw_hrc6000_task()
{
    while (1U)
    {
    	taskENTER_CRITICAL();
    	uint32_t tmp_timer_hrc6000task=timer_hrc6000task;
    	taskEXIT_CRITICAL();
    	if (tmp_timer_hrc6000task==0)
    	{
        	taskENTER_CRITICAL();
        	timer_hrc6000task=10;
        	alive_hrc6000task=true;
        	taskEXIT_CRITICAL();

// There seems to be no need to stop the ticks when the melody is playing
    		//if (melody_play==NULL)
    		{
    	    	if (trxGetMode() == RADIO_MODE_DIGITAL)
    	    	{
    				tick_HR_C6000();
    	    	}
    	    	else if (trxGetMode() == RADIO_MODE_ANALOG)
    	    	{
    	    		trx_check_analog_squelch();
    	    	}
    		}
    	}

		vTaskDelay(0);
    }
}

void buildLCDataFromParams(uint8_t *data,uint8_t FLCO,uint32_t srcId,uint32_t dstId)
{
	data[0] = FLCO;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = (dstId >> 16) & 0xFF;
	data[4] = (dstId >> 8) & 0xFF;
	data[5] = (dstId >> 0) & 0xFF;
	data[6] = (srcId >> 16) & 0xFF;
	data[7] = (srcId >> 8) & 0xFF;
	data[8] = (srcId >> 0) & 0xFF;
	data[9] = 0x00;
	data[10] = 0x00;
	data[11] = 0x00;
}

void buildLC_DataFromLD_Data(uint8_t *outData,uint8_t *LC_DataBytes)
{
	memcpy(outData,LC_DataBytes,9);
	outData[9] = 0x00;
	outData[10] = 0x00;
	outData[11] = 0x00;
}

void setupPcOrTGHeader()
{
	spi_tx[0] = (trxTalkGroupOrPcId >> 24) & 0xFF;
	spi_tx[1] = 0x00;
	spi_tx[2] = 0x00;
	spi_tx[3] = (trxTalkGroupOrPcId >> 16) & 0xFF;
	spi_tx[4] = (trxTalkGroupOrPcId >> 8) & 0xFF;
	spi_tx[5] = (trxTalkGroupOrPcId >> 0) & 0xFF;
	spi_tx[6] = (trxDMRID >> 16) & 0xFF;
	spi_tx[7] = (trxDMRID >> 8) & 0xFF;
	spi_tx[8] = (trxDMRID >> 0) & 0xFF;
	spi_tx[9] = 0x00;
	spi_tx[10] = 0x00;
	spi_tx[11] = 0x00;
	write_SPI_page_reg_bytearray_SPI0(0x02, 0x00, spi_tx, 0x0c);
}

#if false

// Beta testers limiting functionality

	//const uint32_t USER_ID = 5054125;//vk4jwt
	//const uint32_t USER_ID = 5057058;//vk7zja
	//const uint32_t USER_ID = 5057005;//clayton
	const uint32_t USER_ID = 2345496;// G3UBY
	//const uint32_t USER_ID = 5054202;//wayne Vk4wh
	//const uint32_t USER_ID = 2644462;// DG4KLU
	//const uint32_t USER_ID = 2351215;// G4EML
	//const uint32_t USER_ID = 3109921;// 	W1RHS
	//const uint32_t USER_ID = 2420047;// 	LB9AB
	const uint32_t FILTER_LIST[] = {5053238,2345496,5054125,5057058,5057005,5054202,2644462,2351215,3109921,2420047,3109230,3109354,2346139};
#endif
bool callAcceptFilter()
{
	 if (settingsUsbMode == USB_MODE_HOTSPOT)
	 {
  		    //In Hotspot mode, we need to accept all incoming traffic, otherwise private calls won't work
			if (DMR_frame_buffer[0]==TG_CALL_FLAG || DMR_frame_buffer[0]==PC_CALL_FLAG)
			{
#if false
				int numFilters = sizeof(FILTER_LIST)/sizeof(uint32_t);
				for(int i=0;i<numFilters;i++)
				{
					if (receivedSrcId ==FILTER_LIST [i])
					{
						return true;
					}
				}
				return false;
#endif
				return true;
			}
			else
			{
				return false;// Not a PC or TG call
			}
	 }
	 else
	 {
		 return ( (DMR_frame_buffer[0]==TG_CALL_FLAG) || (DMR_frame_buffer[0]==PC_CALL_FLAG && receivedTgOrPcId == trxDMRID));
	 }
}

void tick_HR_C6000()
{
	bool tmp_int_sys=false;
	bool tmp_int_ts=false;
	taskENTER_CRITICAL();
	if (int_sys)
	{
		tmp_int_sys=true;
		int_sys=false;
	}
	if (int_ts)
	{
		tmp_int_ts=true;
		int_ts=false;
	}
	taskEXIT_CRITICAL();

	if (trxIsTransmitting==true && (slot_state == DMR_STATE_IDLE)) // Start TX (first step)
	{
		if (settingsUsbMode != USB_MODE_HOTSPOT)
		{
			init_codec();
			setupPcOrTGHeader();
		}
		else
		{
			write_SPI_page_reg_bytearray_SPI0(0x02, 0x00, (uint8_t *)&audioAndHotspotDataBuffer.hotspotBuffer[wavbuffer_read_idx], 0x0c);// put LC into hardware
		}

		write_SPI_page_reg_byte_SPI0(0x04, 0x40, 0xE3); // TX and RX enable
		write_SPI_page_reg_byte_SPI0(0x04, 0x21, 0xA2); // reset vocoder codingbuffer
		write_SPI_page_reg_byte_SPI0(0x04, 0x22, 0x86); // I2S master encode start

		slot_state = DMR_STATE_TX_START_1;
	}

	// Timeout interrupt
	if (slot_state != DMR_STATE_IDLE)
	{
		if (int_timeout<200)
		{
			int_timeout++;
			if (int_timeout==200)
			{
	            	init_digital();
	            	slot_state = DMR_STATE_IDLE;
	            	int_timeout=0;
#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
            	SEGGER_RTT_printf(0, ">>> INTERRUPT TIMEOUT\r\n");
#endif
			}
		}
	}
	else
	{
		int_timeout=0;
	}

	if (tmp_int_ts)
	{
		// Enable RX or TX
		bool tmp_int_rxtx=false;
		taskENTER_CRITICAL();
		if (int_rxtx)
		{
			tmp_int_rxtx=true;
			int_rxtx=false;
		}
		bool tmp_tx_required=tx_required;
		taskEXIT_CRITICAL();
		if (tmp_int_rxtx)
		{
			if (tmp_tx_required)
			{
				trx_activateTX();
			}
			else
			{
				trx_deactivateTX();
			}
		}

		// RX/TX state machine
		switch (slot_state)
		{
		case DMR_STATE_RX_1: // Start RX (first step)
			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x00);
			GPIO_PinWrite(GPIO_speaker_mute, Pin_speaker_mute, 1);
		    //GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 1);
			slot_state = DMR_STATE_RX_2;
			break;
		case DMR_STATE_RX_2: // Start RX (second step)
			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x50);
			slot_state = DMR_STATE_RX_1;
			break;
		case DMR_STATE_RX_END: // Stop RX
			init_digital_DMR_RX();
			GPIO_PinWrite(GPIO_speaker_mute, Pin_speaker_mute, 0);
		    GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 0);
			slot_state = DMR_STATE_IDLE;
			break;
		case DMR_STATE_TX_START_1: // Start TX (second step)
			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x80);
			write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x10);
			slot_state = DMR_STATE_TX_START_2;
			break;
		case DMR_STATE_TX_START_2: // Start TX (third step)
			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x40); // RXnextslotenable
			slot_state = DMR_STATE_TX_START_3;
			break;
		case DMR_STATE_TX_START_3: // Start TX (fourth step)
			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x80);
			write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x10);
			slot_state = DMR_STATE_TX_START_4;
			break;
		case DMR_STATE_TX_START_4: // Start TX (fifth step)
			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x40); // RXnextslotenable
			slot_state = DMR_STATE_TX_START_5;
			break;
		case DMR_STATE_TX_START_5: // Start TX (sixth step)
            if (settingsUsbMode != USB_MODE_HOTSPOT)
            {
            	tick_TXsoundbuffer();
            }
			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x80);
			write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x10);
			tx_sequence=0;
			slot_state = DMR_STATE_TX_1;
			break;
		case DMR_STATE_TX_1: // Ongoing TX (inactive timeslot)
			if ((trxIsTransmitting==false) && (tx_sequence==0))
			{
				slot_state = DMR_STATE_TX_END_1; // only exit here to ensure staying in the correct timeslot
			}
			else
			{
				write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x40); // RXnextslotenable
				slot_state = DMR_STATE_TX_2;
			}
			break;
		case DMR_STATE_TX_2: // Ongoing TX (active timeslot)

			memset(DMR_frame_buffer, 0, 27 + 0x0C);// fills the LC and  audio buffer with zeros in case there is no real audio

			if (trxIsTransmitting)
			{

                if (settingsUsbMode != USB_MODE_HOTSPOT)
                {
                	tick_TXsoundbuffer();
    				tick_codec_encode(DMR_frame_buffer+0x0C);
                }
                else
                {
                	if (wavbuffer_count > 0)
                	{
						memcpy(DMR_frame_buffer,(uint8_t *)&audioAndHotspotDataBuffer.hotspotBuffer[wavbuffer_read_idx],27+0x0C);

						if(tx_sequence==0)
						{
							write_SPI_page_reg_bytearray_SPI0(0x02, 0x00, DMR_frame_buffer, 0x0c);// put LC into hardware
						}

						wavbuffer_read_idx++;
						if (wavbuffer_read_idx > (HOTSPOT_BUFFER_COUNT-1))
						{
							wavbuffer_read_idx=0;
						}

						if (wavbuffer_count>0)
						{
							wavbuffer_count--;
						}

                	}
                }
    			write_SPI_page_reg_bytearray_SPI1(0x03, 0x00, DMR_frame_buffer+0x0C, 27);// send the audio bytes to the hardware
			}
			else
			{
				write_SPI_page_reg_bytearray_SPI1(0x03, 0x00, (uint8_t *)SILENCE_AUDIO, 27);// send silence audio bytes
			}


			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x80); // TXnextslotenable
			switch (tx_sequence)
			{
			case 0:
				write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x08); // LCSS = 0
				break;
			case 1:
				write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x18); // LCSS = 0
				break;
			case 2:
				write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x28); // LCSS = 0
				break;
			case 3:
				write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x38); // LCSS = 0
				break;
			case 4:
				write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x48); // LCSS = 0
				break;
			case 5:
				write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x58); // LCSS = 0
				break;
			}
			tx_sequence++;
			if (tx_sequence>5)
			{
				tx_sequence=0;
			}
			slot_state = DMR_STATE_TX_1;
			break;
		case DMR_STATE_TX_END_1: // Stop TX (first step)
			write_SPI_page_reg_bytearray_SPI1(0x03, 0x00, SILENCE_AUDIO, 27);// send silence audio bytes
			write_SPI_page_reg_byte_SPI0(0x04, 0x41, 0x80);
			write_SPI_page_reg_byte_SPI0(0x04, 0x50, 0x20);
			slot_state = DMR_STATE_TX_END_2;
			break;
		case DMR_STATE_TX_END_2: // Stop TX (second step)
			write_SPI_page_reg_byte_SPI0(0x04, 0x40, 0xC3);
			init_digital_DMR_RX();
			txstopdelay=30;
			slot_state = DMR_STATE_IDLE;
			break;
		}

		// Timeout interrupted RX
    	if ((slot_state < DMR_STATE_TX_START_1) && (tick_cnt<10))
    	{
    		tick_cnt++;
            if (tick_cnt==10)
            {
            	slot_state = DMR_STATE_RX_END;
#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
            	SEGGER_RTT_printf(0, ">>> TIMEOUT\r\n");
#endif
            }
    	}
	}

	if (tmp_int_sys)
	{
		memset(DMR_frame_buffer,0,256);
		read_SPI_page_reg_byte_SPI0(0x04, 0x82, &tmp_val_0x82);
		read_SPI_page_reg_byte_SPI0(0x04, 0x86, &tmp_val_0x86);
		read_SPI_page_reg_byte_SPI0(0x04, 0x51, &tmp_val_0x51);
		read_SPI_page_reg_byte_SPI0(0x04, 0x52, &tmp_val_0x52);
		read_SPI_page_reg_byte_SPI0(0x04, 0x57, &tmp_val_0x57);
		read_SPI_page_reg_byte_SPI0(0x04, 0x5f, &tmp_val_0x5f);
		read_SPI_page_reg_bytearray_SPI0(0x02, 0x00, DMR_frame_buffer, 64);//0x0c);

#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
           	SEGGER_RTT_printf(0, "DATA %02x [%02x %02x] %02x %02x %02x %02x SC:%02x RCRC:%02x RPI:%02x RXDT:%02x LCSS:%02x TC:%02x AT:%02x CC:%02x ??:%02x ST:%02x RAM:", slot_state, tmp_val_0x82, tmp_val_0x86, tmp_val_0x51, tmp_val_0x52, tmp_val_0x57, tmp_val_0x5f, (tmp_val_0x51 >> 0) & 0x03, (tmp_val_0x51 >> 2) & 0x01, (tmp_val_0x51 >> 3) & 0x01, (tmp_val_0x51 >> 4) & 0x0f, (tmp_val_0x52 >> 0) & 0x03, (tmp_val_0x52 >> 2) & 0x01, (tmp_val_0x52 >> 3) & 0x01, (tmp_val_0x52 >> 4) & 0x0f, (tmp_val_0x57 >> 2) & 0x01, (tmp_val_0x5f >> 0) & 0x03);
			/*	for (int i=0;i<32;i++)
				{
	            	SEGGER_RTT_printf(0, " %02x", DMR_frame_buffer[i]);
				}
            	SEGGER_RTT_printf(0, "\r\n");*/
#endif

		// Check for correct received packet
		int rcrc = (tmp_val_0x51 >> 2) & 0x01;
		int rpi = (tmp_val_0x51 >> 3) & 0x01;
		int cc = (tmp_val_0x52 >> 4) & 0x0f;
        if ((rcrc==0) && (rpi==0) && (cc == trxGetDMRColourCode()) && (slot_state < DMR_STATE_TX_START_1))
        {
		    GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 1);// Turn the LED on as soon as a DMR signal is detected.
    		if (tmp_val_0x82 & 0x20) // InterSendStop
    		{
    			if (tmp_val_0x86 & 0x10)
    			{
// deprecated. Use Segger print if necessary    				send_packet(0x20, 0x10, -1);
    			}
    			if (tmp_val_0x86 & 0x04)
    			{
// deprecated. Use Segger print if necessary    				send_packet(0x20, 0x04, -1);
    			}

    			write_SPI_page_reg_byte_SPI0(0x04, 0x83, 0x20);
    		}

    		receivedTgOrPcId 	= (DMR_frame_buffer[3]<<16)+(DMR_frame_buffer[4]<<8)+(DMR_frame_buffer[5]<<0);// used by the call accept filter
    		receivedSrcId 		= (DMR_frame_buffer[6]<<16)+(DMR_frame_buffer[7]<<8)+(DMR_frame_buffer[8]<<0);// used by the call accept filter

    		if (tmp_val_0x82 & 0x10) // InterLateEntry
    		{
    			// Late entry into ongoing RX
                if (slot_state == DMR_STATE_IDLE && callAcceptFilter())
                {
                	slot_state = DMR_STATE_RX_1;
                	store_qsodata();
                	init_codec();
                	skip_count = 2;

#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
					SEGGER_RTT_printf(0, ">>> RX START LATE\r\n");
#endif
					if (settingsUsbMode == USB_MODE_HOTSPOT)
					{
						DMR_frame_buffer[27 + 0x0c] = HOTSPOT_RX_START_LATE;
						hotspotRxFrameHandler(DMR_frame_buffer);
					}

                }

#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
            	SEGGER_RTT_printf(0, "LATE %02x [%02x %02x] %02x %02x %02x %02x SC:%02x RCRC:%02x RPI:%02x RXDT:%02x LCSS:%02x TC:%02x AT:%02x CC:%02x ??:%02x ST:%02x RAM:", slot_state, tmp_val_0x82, tmp_val_0x86, tmp_val_0x51, tmp_val_0x52, tmp_val_0x57, tmp_val_0x5f, (tmp_val_0x51 >> 0) & 0x03, (tmp_val_0x51 >> 2) & 0x01, (tmp_val_0x51 >> 3) & 0x01, (tmp_val_0x51 >> 4) & 0x0f, (tmp_val_0x52 >> 0) & 0x03, (tmp_val_0x52 >> 2) & 0x01, (tmp_val_0x52 >> 3) & 0x01, (tmp_val_0x52 >> 4) & 0x0f, (tmp_val_0x57 >> 2) & 0x01, (tmp_val_0x5f >> 0) & 0x03);
				for (int i=0;i<0x0c;i++)
				{
	            	SEGGER_RTT_printf(0, " %02x", DMR_frame_buffer[i]);
				}
            	SEGGER_RTT_printf(0, "\r\n");
#endif

// deprecated. Use Segger print if necessary    			send_packet(0x10, 0x00, -1);

    			write_SPI_page_reg_byte_SPI0(0x04, 0x83, 0x10);
    		}

    		if (tmp_val_0x82 & 0x08) // InterRecvData
    		{
    			// Reset RX timeout
    			tick_cnt = 0;

    			// Start RX
    			int rxdt = (tmp_val_0x51 >> 4) & 0x0f;
    			int sc = (tmp_val_0x51 >> 0) & 0x03;
                if ((slot_state == DMR_STATE_IDLE) && (sc==2) && (rxdt==1) &&  callAcceptFilter())
                {
                	slot_state = DMR_STATE_RX_1;
                	store_qsodata();
                	init_codec();
                	skip_count = 0;
#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
            	SEGGER_RTT_printf(0, ">>> RX START\r\n");
#endif
					if (settingsUsbMode == USB_MODE_HOTSPOT)
					{
						DMR_frame_buffer[27 + 0x0c] = HOTSPOT_RX_START;
						hotspotRxFrameHandler(DMR_frame_buffer);
					}
                }
    			// Stop RX
                if ((sc==2) && (rxdt==2) && callAcceptFilter())
                {
                	slot_state = DMR_STATE_RX_END;
#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
            	SEGGER_RTT_printf(0, ">>> RX STOP\r\n");
#endif

					if (settingsUsbMode == USB_MODE_HOTSPOT)
					{
						DMR_frame_buffer[27 + 0x0c] = HOTSPOT_RX_STOP;
						hotspotRxFrameHandler(DMR_frame_buffer);
					}
                }

            	if ((slot_state!=0) && (skip_count>0) && (sc!=2) && ((rxdt & 0x07) == 0x01))
            	{
            		skip_count--;
            	}

                // Detect/decode voice packet and transfer it into the output soundbuffer
                if ((slot_state != DMR_STATE_IDLE) && (skip_count==0) && (sc!=2) && ((rxdt & 0x07) >= 0x01) && ((rxdt & 0x07) <= 0x06))
                {
#if false
                	SEGGER_RTT_printf(0, ">>> Audio frame %02x\r\n",rxdt & 0x07);
#endif
                	store_qsodata();
                    read_SPI_page_reg_bytearray_SPI1(0x03, 0x00, DMR_frame_buffer+0x0C, 27);
                    if (settingsUsbMode == USB_MODE_HOTSPOT)
                    {
   						DMR_frame_buffer[27 + 0x0c] = HOTSPOT_RX_AUDIO_FRAME;
   						DMR_frame_buffer[27 + 0x0c + 1] = (rxdt & 0x07);// audio sequence number
   						hotspotRxFrameHandler(DMR_frame_buffer);
                    }
                    else
                    {
                    	if (settingsPrivateCallMuteMode == false)
                    	{
                    		tick_codec_decode(DMR_frame_buffer+0x0C);
                    		tick_RXsoundbuffer();
                    	}
                    }
                }
                else
                {
#if false
                    if (settingsUsbMode == USB_MODE_HOTSPOT)
                    {
   						DMR_frame_buffer[27 + 0x0c] = HOTSPOT_RX_IDLE_OR_REPEAT;
   						hotspotRxFrameHandler(DMR_frame_buffer);
                    }

                	SEGGER_RTT_printf(0, ">>> Not valid data (perhaps Idle) or another Voice header LC frame\r\n");
#endif
                }

#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
                SEGGER_RTT_printf(0, "DATA %02x [%02x %02x] %02x %02x %02x %02x SC:%02x RCRC:%02x RPI:%02x RXDT:%02x LCSS:%02x TC:%02x AT:%02x CC:%02x ??:%02x ST:%02x RAM:", slot_state, tmp_val_0x82, tmp_val_0x86, tmp_val_0x51, tmp_val_0x52, tmp_val_0x57, tmp_val_0x5f, (tmp_val_0x51 >> 0) & 0x03, (tmp_val_0x51 >> 2) & 0x01, (tmp_val_0x51 >> 3) & 0x01, (tmp_val_0x51 >> 4) & 0x0f, (tmp_val_0x52 >> 0) & 0x03, (tmp_val_0x52 >> 2) & 0x01, (tmp_val_0x52 >> 3) & 0x01, (tmp_val_0x52 >> 4) & 0x0f, (tmp_val_0x57 >> 2) & 0x01, (tmp_val_0x5f >> 0) & 0x03);
				for (int i=0;i<0x0c;i++)
				{
	            	SEGGER_RTT_printf(0, " %02x", DMR_frame_buffer[i]);
				}
            	SEGGER_RTT_printf(0, "\r\n");
#endif

// deprecated. Use Segger print if necessary	send_packet(0x08, 0x00, -1);

    			write_SPI_page_reg_byte_SPI0(0x04, 0x83, 0x08);
    		}

    		if (tmp_val_0x82 & 0x01) // InterPHYOnly
    		{
// deprecated. Use Segger print if necessary  	send_packet(0x01, 0x00, -1);

    			write_SPI_page_reg_byte_SPI0(0x04, 0x83, 0x01);
    		}

    		if (tmp_val_0x82 & 0xC6)
    		{
// deprecated. Use Segger print if necessary     send_packet(0xFF, 0xFF, -1);

    			write_SPI_page_reg_byte_SPI0(0x04, 0x83, 0xC6);
    		}
        }
        else if (slot_state >= DMR_STATE_TX_START_1)
        {
        	uint8_t tmp_val_0x42;
			read_SPI_page_reg_byte_SPI0(0x04, 0x42, &tmp_val_0x42);
#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
            	SEGGER_RTT_printf(0, "TXTX %02x [%02x %02x] %02x\r\n", slot_state, tmp_val_0x82, tmp_val_0x86, tmp_val_0x42);
#endif
    		write_SPI_page_reg_byte_SPI0(0x04, 0x83, 0xFF);
        }
        else
        {
#if defined(USE_SEGGER_RTT) && defined(DEBUG_DMR_DATA)
            	SEGGER_RTT_printf(0, "---- %02x [%02x %02x] %02x %02x %02x %02x SC:%02x RCRC:%02x RPI:%02x RXDT:%02x LCSS:%02x TC:%02x AT:%02x CC:%02x ??:%02x ST:%02x RAM:", slot_state, tmp_val_0x82, tmp_val_0x86, tmp_val_0x51, tmp_val_0x52, tmp_val_0x57, tmp_val_0x5f, (tmp_val_0x51 >> 0) & 0x03, (tmp_val_0x51 >> 2) & 0x01, (tmp_val_0x51 >> 3) & 0x01, (tmp_val_0x51 >> 4) & 0x0f, (tmp_val_0x52 >> 0) & 0x03, (tmp_val_0x52 >> 2) & 0x01, (tmp_val_0x52 >> 3) & 0x01, (tmp_val_0x52 >> 4) & 0x0f, (tmp_val_0x57 >> 2) & 0x01, (tmp_val_0x5f >> 0) & 0x03);
				for (int i=0;i<0x0c;i++)
				{
	            	SEGGER_RTT_printf(0, " %02x", DMR_frame_buffer[i]);
				}
            	SEGGER_RTT_printf(0, "\r\n");
#endif
        }
	}

	if (qsodata_timer>0)
	{
		// Only timeout the QSO data display if not displaying the Private Call Accept Yes/No text
		// if menuUtilityReceivedPcId is non zero the Private Call Accept text is being displayed
		if (menuUtilityReceivedPcId == 0)
		{
			qsodata_timer--;

			if (qsodata_timer==0)
			{
				menuDisplayQSODataState= QSO_DISPLAY_DEFAULT_SCREEN;
			}
		}

	}
}
