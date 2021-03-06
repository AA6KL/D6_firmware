/* Copyright (C) 
 *		2019 Jose Luu
 * additional authors:
 *
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "memory.h"
#include "spi.h"
#include "D6Board.h"
#include "Command.h"
#include "main.h"
#include "ADF4351.h"

#define ADF_COUNT 2
#define ADF_REGISTER_COUNT 6

static unsigned long Register_Set[ADF_COUNT][ADF_REGISTER_COUNT];
static unsigned long Register_SetPrevious[ADF_COUNT][ADF_REGISTER_COUNT];

void ADF4351Off(WhichADF indexADF);


void loadEnable(WhichADF indexADF, bool low)
{
	switch (indexADF) 
	{
	case eTracking:
		HAL_GPIO_WritePin(LE_GPIO_Port,
		LE_Pin,
		(low ? GPIO_PIN_RESET : GPIO_PIN_SET));
		break;
	case eAnalyzer:
		HAL_GPIO_WritePin(LEA_GPIO_Port,
				LEA_Pin,
				(low ? GPIO_PIN_RESET : GPIO_PIN_SET));
		break;
	}
}
void sendADF4351(WhichADF indexADF, unsigned long reg)
{
	loadEnable(indexADF, true);
	switch(indexADF)
	{
	case eTracking:
		sendSPI2(reg);
		break;
	case eAnalyzer:
		sendSPI1(reg);
		break;
	}
	loadEnable(indexADF, false);
}

void writeADFRegisters(WhichADF indexADF, unsigned long registerSet[])
{
	//	_SYNC(1); // measure time to write to ADF
	for (int i = ADF_REGISTER_COUNT-1; i >= 0; i--) 
	{
		bool mustWrite = false;
		if (mustWrite || (registerSet[i] != Register_SetPrevious[indexADF][i])) 
		{
			unsigned long Reg_Buf;
			mustWrite = true; // we must finish up to register 0 once we start writing into registers
			Reg_Buf = registerSet[i];
			sendADF4351(indexADF,Reg_Buf);
			Register_SetPrevious[indexADF][i] = registerSet[i];// remember value so that we can skip it next time
		}
	}
	//	_SYNC(0);
}


void frequencySetupInternal(WhichADF indexADF, unsigned long long internalVCOFrequency, unsigned int rangeFrequency, unsigned int level)
{
	unsigned long intDivider = internalVCOFrequency / REFERENCE; // see fig 25 page 15 of ADF4341 document
	unsigned long frac = internalVCOFrequency / CHANNEL_SPACING % MODULUS_VALUE;

	unsigned long registerSet[6];

	registerSet[0] = ((intDivider & INT_VALUE_MASK) << INT_VALUE_SHIFT) |
						((frac & FRAC_VALUE_MASK) << FRAC_VALUE_SHIFT);
	registerSet[1] = R1;
	registerSet[2] = R2;
	registerSet[3] = R3;
	registerSet[4] = R4;
	registerSet[4] |= ((rangeFrequency & RF_DIVIDER_SELECT_MASK) << RF_DIVIDER_SELECT_SHIFT);
	switch (level)
	{
	case 0: // RF will be completely off
		registerSet[4] &= (~RF_OUTPUT_ENABLE);
		break;
	default:
	case 1:
		registerSet[4] |= RF_OUTPUT_MINUS4dBm;
		break;
	case 2:
		registerSet[4] |= RF_OUTPUT_MINUS1dBm;
		break;
	case 3:
		registerSet[4] |= RF_OUTPUT_2dBm;
		break;
	case 4:
		registerSet[4] |= RF_OUTPUT_5dBm;
		break;
	}
	registerSet[5] = R5;
	writeADFRegisters(indexADF, registerSet);
}

bool frequencySetup(WhichADF indexADF, unsigned long long frequency, unsigned int level, unsigned int &range, bool forceRange)
{
	unsigned long long VCOfrequency = frequency;
	if (VCOfrequency > ADF_MAX_FREQUENCY) 
	{
		ADF4351Off(indexADF);
		return false;
	}
	if (forceRange) 
	{
		VCOfrequency = frequency << range;
		if (VCOfrequency< ADF_MIN_FREQUENCY || VCOfrequency > ADF_MAX_FREQUENCY) 
		{
			return false;
		}
		frequencySetupInternal(indexADF, VCOfrequency, range, level);
		return true;
	}
	for (range = 0; range < ADF_RANGE_COUNT; range++)
	{
		if (VCOfrequency >= ADF_LOW_FREQUENCY)
		{
			frequencySetupInternal(indexADF, VCOfrequency, range, level);
			return true;
		}
		VCOfrequency *= 2;
	}
	if (VCOfrequency/2 >= ADF_MIN_FREQUENCY){ // we try the extended value
		range--;
		frequencySetupInternal(indexADF, VCOfrequency/2, range, level);
		return true;
	}
	ADF4351Off(indexADF);
	return false;
}

void ADF4351Off(WhichADF indexADF)
{
	unsigned long registerSet[6];

	registerSet[0] = 0;
	registerSet[1] = R1;
	registerSet[2] = R2;
	registerSet[3] = R3;
	registerSet[4] = R4 | VCO_POWERED_DOWN;
	registerSet[5] = R5;
	writeADFRegisters(indexADF, registerSet);
}

void probeSPI(WhichADF indexADF) // write arbitrary patterns to check SPI
{
	unsigned long registerSet[6];
	registerSet[0] = 0x55555501;
	registerSet[1] = 0x55550101;
	registerSet[2] = 0x55010101;
	registerSet[3] = 0xAAAAAA55;
	registerSet[4] = 0xAAAA0101;
	registerSet[5] = 0x80880088;
	writeADFRegisters(indexADF, &registerSet[0]);
}