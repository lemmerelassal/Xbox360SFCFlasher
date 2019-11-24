// SPI.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#define	SFCX_CONFIG	0x00	//
#define SFCX_STATUS	0x04	//10
#define SFCX_CMD	0x08
#define	SFCX_ADDR	0x0C
#define SFCX_DATA	0x10

#define SCK			0x01
#define MOSI		0x02
#define MISO		0x04
#define SS			0x08
#define SMC_DBG_EN	0x10
#define SMC_RST		0x20

	FT_STATUS ftStatus; // Result of each D2XX call
	DWORD dwNumDevs; // The number of devices
	unsigned int uiDevIndex = 0xF; // The device in the list that we'll use
	BYTE byOutputBuffer[65536]; // Buffer to hold MPSSE commands and data
								//	to be sent to the FT2232H
	BYTE byInputBuffer[65536]; // Buffer to hold data read from the FT2232H
	DWORD dwCount = 0; // General loop index
	DWORD dwNumBytesToSend = 0; // Index to the output buffer
	DWORD dwNumBytesSent = 0; // Count of actual bytes sent - used with FT_Write
	DWORD dwNumBytesToRead = 0; // Number of bytes available to read
								// in the driver's input buffer
	DWORD dwNumBytesToReceive = 0;
	DWORD dwNumBytesRead = 0; // Count of actual bytes read - used with FT_Read
	DWORD dwClockDivisor = 0x3FFF; //0x05DB; // Value of clock divisor, SCL Frequency =

	BYTE byGPIOs = 0;
	BYTE byDirections = 0;


FT_STATUS SPI_InitDevice(FT_HANDLE* pftHandle)
{
	FT_HANDLE ftHandle; // Handle of the FTDI device
	ftStatus = FT_CreateDeviceInfoList(&dwNumDevs);
	// Get the number of FTDI devices
	if (ftStatus != FT_OK) // Did the command execute OK?
	{
		printf("Error in getting the number of devices\n");
		return 1; // Exit with error
	}
	if (dwNumDevs < 1) // Exit if we don't see any
	{
		printf("There are no FTDI devices installed\n");
		return 1; // Exit with error
	}
	printf("%d FTDI devices found \n- the count includes individual ports on a single chip\n", dwNumDevs);

	ftStatus = FT_Open(0, &ftHandle);
	if (ftStatus != FT_OK)
	{
		printf("Open Failed with error %d\n", ftStatus);
		return 1; // Exit with error
	}


	// Configure port parameters
	printf("\nConfiguring port for MPSSE use...\n");
	ftStatus |= FT_ResetDevice(ftHandle);	//Reset USB device

	//Purge USB receive buffer first by reading out all old data from FT2232H receive buffer
	ftStatus |= FT_GetQueueStatus(ftHandle, &dwNumBytesToRead);	// Get the number of bytes in the FT2232H
	// receive buffer
	if ((ftStatus == FT_OK) && (dwNumBytesToRead > 0))
		FT_Read(ftHandle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead);
	//Read out the data from FT2232H receive buffer
	ftStatus |= FT_SetUSBParameters(ftHandle, 65536, 65535);
	//Set USB request transfer sizes to 64K
	ftStatus |= FT_SetChars(ftHandle, false, 0, false, 0);
	//Disable event and error characters
	ftStatus |= FT_SetTimeouts(ftHandle, 0, 5000);
	//Sets the read and write timeouts in milliseconds
	ftStatus |= FT_SetLatencyTimer(ftHandle, 0);
	//Set the latency timer to 1mS (default is 16mS)
	ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x00);
	//Reset controller
	ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x02);
	//Enable MPSSE mode
	if (ftStatus != FT_OK)
	{
		printf("Error in initializing the MPSSE %d\n", ftStatus);
		FT_Close(ftHandle);
		return 1; // Exit with error
	}
	Sleep(50); // Wait for all the USB stuff to complete and work
	// Enable internal loop-back
	byOutputBuffer[dwNumBytesToSend++] = 0x84;
	// Enable loopback
	ftStatus = FT_Write(ftHandle, byOutputBuffer, \
	dwNumBytesToSend, &dwNumBytesSent);
	// Send off the loopback command
	dwNumBytesToSend = 0; // Reset output buffer pointer
	// Check the receive buffer - it should be empty
	ftStatus = FT_GetQueueStatus(ftHandle, &dwNumBytesToRead);
	// Get the number of bytes in
	// the FT2232H receive buffer
	if (dwNumBytesToRead != 0)
	{
	printf("Error - MPSSE receive buffer should be empty\n", ftStatus);
	FT_SetBitMode(ftHandle, 0x0, 0x00);
	// Reset the port to disable MPSSE
	FT_Close(ftHandle); // Close the USB port
	return 1; // Exit with error
	}
	// -----------------------------------------------------------
	// Synchronize the MPSSE by sending a bogus opcode (0xAB),
	// The MPSSE will respond with "Bad Command" (0xFA) followed by
	// the bogus opcode itself.
	// -----------------------------------------------------------
	byOutputBuffer[dwNumBytesToSend++] = 0xAB;
	//Add bogus command ‘0xAB’ to the queue
	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	// Send off the BAD command
	dwNumBytesToSend = 0; // Reset output buffer pointer
	do
	{
		ftStatus = FT_GetQueueStatus(ftHandle, &dwNumBytesToRead);
		// Get the number of bytes in the device input buffer
	} while ((dwNumBytesToRead == 0) && (ftStatus == FT_OK));
	//or Timeout
	bool bCommandEchod = false;
	ftStatus = FT_Read(ftHandle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead);
	//Read out the data from input buffer
	for (dwCount = 0; dwCount < dwNumBytesRead - 1; dwCount++)
	//Check if Bad command and echo command are received
	{
		if ((byInputBuffer[dwCount] == 0xFA) && (byInputBuffer[dwCount+1] == 0xAB))
		{
			bCommandEchod = true;
			break;
		}
	}
	if (bCommandEchod == false)
	{
		printf("Error in synchronizing the MPSSE\n");
		FT_Close(ftHandle);
		return 1; // Exit with error
	}
	// Disable internal loop-back
	byOutputBuffer[dwNumBytesToSend++] = 0x85;
	// Disable loopback
	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	// Send off the loopback command
	dwNumBytesToSend = 0; // Reset output buffer pointer
	// Check the receive buffer - it should be empty
	ftStatus = FT_GetQueueStatus(ftHandle, &dwNumBytesToRead);
	// Get the number of bytes in
	// the FT2232H receive buffer
	if (dwNumBytesToRead != 0)
	{
		printf("Error - MPSSE receive buffer should be empty\n", ftStatus);
		FT_SetBitMode(ftHandle, 0x0, 0x00);
		// Reset the port to disable MPSSE
		FT_Close(ftHandle); // Close the USB port
		return FT_OTHER_ERROR; // Exit with error
	}

	dwNumBytesToSend = 0; // Start with a fresh index
	// Set up the Hi-Speed specific commands for the FTx232H
	byOutputBuffer[dwNumBytesToSend++] = 0x8A;
	// Use 60MHz master clock (disable divide by 5)
	byOutputBuffer[dwNumBytesToSend++] = 0x97;
	// Turn off adaptive clocking (may be needed for ARM)
	byOutputBuffer[dwNumBytesToSend++] = 0x8D;
	// Disable three-phase clocking
	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	// Send off the HS-specific commands
	dwNumBytesToSend = 0; // Reset output buffer pointer
	// Set TCK frequency
	// TCK = 60MHz /((1 + [(1 +0xValueH*256) OR 0xValueL])*2)
	byOutputBuffer[dwNumBytesToSend++] = 0x86;
	// Command to set clock divisor
	byOutputBuffer[dwNumBytesToSend++] = dwClockDivisor & 0xFF;
	// Set 0xValueL of clock divisor
	byOutputBuffer[dwNumBytesToSend++] = (dwClockDivisor >> 8) & 0xFF;
	// Set 0xValueH of clock divisor
	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	// Send off the clock divisor commands
	dwNumBytesToSend = 0; // Reset output buffer pointer
	dwNumBytesToRead = 0;

	*pftHandle = ftHandle;

	return FT_OK;
}

FT_STATUS SPI_SetGPIOs(FT_HANDLE ftHandle, UCHAR byMask, UCHAR byValue)
{
	dwNumBytesToSend = 0;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byValue;
	byOutputBuffer[dwNumBytesToSend++] = byMask;
	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	if(ftStatus == FT_OK)
		while(dwNumBytesSent != dwNumBytesToSend)
			printf("Sending byte %d\n", dwNumBytesSent);

	dwNumBytesToRead = 0;
	return ftStatus;
}

FT_STATUS SPI_GetGPIOs(FT_HANDLE ftHandle, PUCHAR pbyValue)
{
	dwNumBytesToSend = 0;
	byOutputBuffer[dwNumBytesToSend++] = 0x81;
	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	do{
		ftStatus = FT_GetQueueStatus(ftHandle, &dwNumBytesToRead);
	}while( (dwNumBytesToRead == 0) &&  (ftStatus == FT_OK) );
	
	if(ftStatus == FT_OK)
		ftStatus = FT_Read(ftHandle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead);
	if(ftStatus == FT_OK)
		*pbyValue = byInputBuffer[0];
	dwNumBytesToRead = 0;
	return ftStatus;
}

FT_STATUS SPI_Close(FT_HANDLE ftHandle)
{
	return FT_Close(ftHandle);
}

FT_STATUS SPI_InitCommands(FT_HANDLE ftHandle)
{
	dwNumBytesToSend = 0;
	do
	{
		dwNumBytesRead = 0;
		dwNumBytesToRead = 0;
		ftStatus = FT_GetQueueStatus(ftHandle, &dwNumBytesToRead);
		if(dwNumBytesToRead)
			ftStatus |= FT_Read(ftHandle, byInputBuffer, dwNumBytesToRead, &dwNumBytesRead);
	} while(dwNumBytesToRead != 0);
	
	dwNumBytesToSend = 0;
	return ftStatus;
}

FT_STATUS SPI_ExecuteCommands(FT_HANDLE ftHandle, PUCHAR pbyInputBuffer)
{
	dwNumBytesToRead = 0;
	dwNumBytesRead = 0;
	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	DWORD temp = GetTickCount();
	if(ftStatus == FT_OK)
		while(dwNumBytesSent != dwNumBytesToSend);
	temp = GetTickCount()-temp;

	do
	{
		ftStatus = FT_GetQueueStatus(ftHandle, &dwNumBytesToRead);
	}while( (dwNumBytesToRead < dwNumBytesToReceive) &&  (ftStatus == FT_OK) );

	if((ftStatus == FT_OK) && (dwNumBytesToRead != 0))
		ftStatus = FT_Read(ftHandle, pbyInputBuffer, dwNumBytesToRead, &dwNumBytesRead);

	if(ftStatus == FT_OK)
		while(dwNumBytesToRead != dwNumBytesRead);
	
	dwNumBytesToReceive = 0;
	dwNumBytesToSend = 0;

	return ftStatus;
}

void SPI_AddReadRegister(FT_HANDLE ftHandle, UCHAR byReg)
{
	byGPIOs &=~ SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	//byOutputBuffer[dwNumBytesToSend++] = 0x19;
	//byOutputBuffer[dwNumBytesToSend++] = 0x01;
	//byOutputBuffer[dwNumBytesToSend++] = 0x00;
	//byOutputBuffer[dwNumBytesToSend++] = (byReg << 2) | 1;
	//byOutputBuffer[dwNumBytesToSend++] = 0xFF;

	byOutputBuffer[dwNumBytesToSend++] = 0x28;
	byOutputBuffer[dwNumBytesToSend++] = 99; //0x03;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byGPIOs |= SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	dwNumBytesToReceive += 100; //4;

	if(dwNumBytesToSend >= 0xFF00)
	{
		ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
		if(ftStatus == FT_OK)
			while(dwNumBytesSent != dwNumBytesToSend);
		dwNumBytesToSend = 0;
		dwNumBytesSent = 0;
	}
}

void SPI_AddWriteRegister(FT_HANDLE ftHandle, UCHAR byReg, DWORD dwValue) 
{
	byGPIOs &=~ SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	byOutputBuffer[dwNumBytesToSend++] = 0x19;
	byOutputBuffer[dwNumBytesToSend++] = 0x04;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byReg += byReg;
	byReg += byReg;
	byReg |= 2;

	byOutputBuffer[dwNumBytesToSend++] = byReg;
	byOutputBuffer[dwNumBytesToSend++] = (dwValue >> 0x00) & 0xFF;
	byOutputBuffer[dwNumBytesToSend++] = (dwValue >> 0x08) & 0xFF;
	byOutputBuffer[dwNumBytesToSend++] = (dwValue >> 0x10) & 0xFF;
	byOutputBuffer[dwNumBytesToSend++] = (dwValue >> 0x18) & 0xFF;

	byGPIOs |= SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	if(dwNumBytesToSend >= 0xFF00)
	{
		ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
		if(ftStatus == FT_OK)
			while(dwNumBytesSent != dwNumBytesToSend);
		dwNumBytesToSend = 0;
		dwNumBytesSent = 0;
	}
}

FT_STATUS SPI_ReadRegister(FT_HANDLE ftHandle, UCHAR byRegister, LPDWORD lpdwValue)
{
	//SPI_InitCommands(ftHandle);
	UCHAR temp[4];
	SPI_AddReadRegister(ftHandle, byRegister);
	SPI_ExecuteCommands(ftHandle, temp);
	*lpdwValue = ((DWORD) temp[3] << 24) | ((DWORD) temp[2] << 16) | ((DWORD) temp[1] << 8) | (DWORD) temp[0];
	return FT_OK;
}

FT_STATUS SPI_WriteRegister(FT_HANDLE ftHandle, UCHAR byRegister, DWORD dwValue)
{
//	SPI_InitCommands(ftHandle);
	SPI_AddWriteRegister(ftHandle, byRegister, dwValue);
	SPI_ExecuteCommands(ftHandle, byInputBuffer);
	return FT_OK;
}

FT_STATUS SPI_DeInitSMC(FT_HANDLE ftHandle)
{
	byDirections = SCK | MOSI | SS | SMC_DBG_EN | SMC_RST;
	
	byGPIOs = SMC_DBG_EN | SMC_RST | SS;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);

	byGPIOs = SMC_DBG_EN | SS;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	Sleep(100);

	byGPIOs = SS;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);

	byGPIOs = SMC_RST | SS;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	return FT_OK;
}

FT_STATUS SPI_InitSMC(FT_HANDLE ftHandle)
{
	byDirections = SCK | MOSI | SS | SMC_DBG_EN | SMC_RST;

	byGPIOs = SS | MOSI | SMC_RST;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	SPI_GetGPIOs(ftHandle, &byGPIOs);
	byGPIOs = SS | MOSI | SMC_RST;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	byGPIOs = SS | MOSI | SMC_RST;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	byGPIOs = SS | SMC_RST;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	byGPIOs = SMC_RST;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	byGPIOs = 0;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	byGPIOs = SMC_DBG_EN;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	byGPIOs = SMC_DBG_EN | SMC_RST;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	byGPIOs = SMC_DBG_EN | SMC_RST;
	SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	Sleep(100);
	return FT_OK;

}

/*
	if(gValue & 0x1) gtemp |= SS;
	if(gValue & 0x2) gtemp |= MOSI;
	if(gValue & 0x4) gtemp |= SMC_DBG_EN;	// non-inverted
	if(gValue & 0x8) gtemp |= SMC_RST;
	if(gValue & 0x10) gtemp |= SCK;
	*/

void EraseBlock(FT_HANDLE ftHandle, DWORD dwBlockNumber)
{
	DWORD temp;

	SPI_ReadRegister(ftHandle, SFCX_CONFIG, &temp);
	temp |= 0x8;
	SPI_WriteRegister(ftHandle, SFCX_CONFIG, temp);
	

	SPI_WriteRegister(ftHandle, SFCX_STATUS, 0xFF);
	SPI_WriteRegister(ftHandle, SFCX_ADDR, dwBlockNumber);
	
	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);

	SPI_WriteRegister(ftHandle, SFCX_CMD, 0xAA);
	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);

	SPI_WriteRegister(ftHandle, SFCX_CMD, 0x55);
	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);

	SPI_WriteRegister(ftHandle, SFCX_CMD, 0x5);

	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);

	SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);
	if(temp != 0x200)
		printf("\nStatus: 0x%X", temp);
	SPI_WriteRegister(ftHandle, SFCX_STATUS, 0xFF);
	SPI_ReadRegister(ftHandle, SFCX_CONFIG, &temp);
	SPI_WriteRegister(ftHandle, SFCX_CONFIG, temp & ~0x8);
}


void WritePage(FT_HANDLE ftHandle, DWORD dwPageNumber, PUCHAR pbyBuffer)
{
	DWORD temp = 0;


	SPI_ReadRegister(ftHandle, SFCX_CONFIG, &temp);
	temp |= 0x8;
	SPI_WriteRegister(ftHandle, SFCX_CONFIG, temp);
	
	// WriteStart
	SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);
	SPI_WriteRegister(ftHandle, SFCX_STATUS, temp);
	SPI_WriteRegister(ftHandle, SFCX_ADDR, 0);

	// WriteProcess
	for(UINT8 i=0; i<0x84; i++)
	{
		SPI_AddWriteRegister(ftHandle, 0x10, *(DWORD*)pbyBuffer);
		SPI_AddWriteRegister(ftHandle, 0x8, 1);
		pbyBuffer+=4;
	}
	SPI_ExecuteCommands(ftHandle, pbyBuffer);


	// WriteExecute
	SPI_WriteRegister(ftHandle, 0xC, dwPageNumber << 9);
	SPI_WriteRegister(ftHandle, 0x8, 0x55);
	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);

	SPI_WriteRegister(ftHandle, 0x8, 0xAA);
	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);

	SPI_WriteRegister(ftHandle, 0x8, 0x4);
	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);
//	SPI_ExecuteCommands(ftHandle, byInputBuffer);
	
	if(temp != 0x200)
		printf("\nStatus: 0x%X", temp);

	SPI_ReadRegister(ftHandle, SFCX_CONFIG, &temp);
	temp &=~ 8;
	SPI_WriteRegister(ftHandle, SFCX_CONFIG, temp);
	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);
}

void ReadPage(FT_HANDLE ftHandle, DWORD dwPageNumber, PUCHAR pbyBuffer)
{
	DWORD temp = 0;
	DWORD i;
	//byGPIOs = SMC_DBG_EN | SMC_RST;
	//SPI_SetGPIOs(ftHandle, byDirections, byGPIOs);
	SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	// OK
	SPI_WriteRegister(ftHandle, SFCX_STATUS, temp);	// OK
	SPI_WriteRegister(ftHandle, SFCX_ADDR, (dwPageNumber << 9)); // OK
	SPI_WriteRegister(ftHandle, SFCX_CMD, 3);	// OK
	do
	{
		SPI_ReadRegister(ftHandle, SFCX_STATUS, &temp);	//OK
	} while(temp & 0x1);

	SPI_WriteRegister(ftHandle, SFCX_ADDR, 0);	// OK
	
	for(i = 0; i < 0x84; i++)
	{
		SPI_AddWriteRegister(ftHandle, 0x08, 0x00);
		SPI_AddReadRegister(ftHandle, 0x10);
	}

	SPI_ExecuteCommands(ftHandle, pbyBuffer);

}

void InitDMA(FT_HANDLE ftHandle)
{
	SPI_WriteRegister(ftHandle, 0x0C, 0x73800);
	SPI_WriteRegister(ftHandle, 0x1C, 0x100);
	SPI_WriteRegister(ftHandle, 0x20, 0x4000);
	SPI_WriteRegister(ftHandle, 0x08, 0x7);
//	SPI_ExecuteCommands(ftHandle, pbyBuffer);
}

void	EEPROM_WriteEnable(FT_HANDLE ftHandle)
{
	dwNumBytesToSend = 0;
	
	byGPIOs = (SS | SMC_RST | SMC_DBG_EN);
	byDirections = SCK | MOSI | SS | SMC_DBG_EN | SMC_RST;
	
	byGPIOs &=~ SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;


	byOutputBuffer[dwNumBytesToSend++] = 0x11;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	byOutputBuffer[dwNumBytesToSend++] = 0x06;

	byGPIOs |= SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;
	
	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	if(ftStatus == FT_OK)
		while(dwNumBytesSent != dwNumBytesToSend);

}

void	EEPROM_WriteDisable(FT_HANDLE ftHandle)
{
	dwNumBytesToSend = 0;
	byGPIOs &=~ SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;


	byOutputBuffer[dwNumBytesToSend++] = 0x11;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	byOutputBuffer[dwNumBytesToSend++] = 0x04;

	byGPIOs |= SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	if(ftStatus == FT_OK)
		while(dwNumBytesSent != dwNumBytesToSend);

}

void	EEPROM_EraseBulk(FT_HANDLE ftHandle)
{
	EEPROM_WriteEnable(ftHandle);

	dwNumBytesToSend = 0;

	byGPIOs &=~ SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	byOutputBuffer[dwNumBytesToSend++] = 0x11;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	byOutputBuffer[dwNumBytesToSend++] = 0xC7;

	byGPIOs |= SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	if(ftStatus == FT_OK)
		while(dwNumBytesSent != dwNumBytesToSend);

	Sleep(1000);
	EEPROM_WriteDisable(ftHandle);
	dwNumBytesToSend = 0;
}

void	EEPROM_ProgramPage(FT_HANDLE ftHandle, DWORD dwAddress, PBYTE pbySrc)
{
	EEPROM_WriteEnable(ftHandle);

	dwNumBytesToSend = 0;

	byGPIOs &=~ SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	byOutputBuffer[dwNumBytesToSend++] = 0x11;
	byOutputBuffer[dwNumBytesToSend++] = 0x83;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	byOutputBuffer[dwNumBytesToSend++] = 0x02;
	byOutputBuffer[dwNumBytesToSend++] = (dwAddress >> 16)	& 0xFF;
	byOutputBuffer[dwNumBytesToSend++] = (dwAddress >> 8)	& 0xFF;
	byOutputBuffer[dwNumBytesToSend++] = (dwAddress >> 0)	& 0xFF;

	memcpy(byOutputBuffer+dwNumBytesToSend, pbySrc, 128);
	dwNumBytesToSend+=0x80;

	byGPIOs |= SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	if(ftStatus == FT_OK)
		while(dwNumBytesSent != dwNumBytesToSend);
	dwNumBytesToSend = 0;

	Sleep(1000);

	EEPROM_WriteDisable(ftHandle);

	dwNumBytesToSend = 0;
}

void	EEPROM_ReadPage(FT_HANDLE ftHandle, DWORD dwAddress, PBYTE pbyDest)	//256 bytes
{
	dwNumBytesToSend = 0;
	
	byGPIOs = (SS | SMC_RST | SMC_DBG_EN);
	byDirections = SCK | MOSI | SS | SMC_DBG_EN | SMC_RST;

	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;




	byGPIOs &=~ SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	byOutputBuffer[dwNumBytesToSend++] = 0x11;
	byOutputBuffer[dwNumBytesToSend++] = 0x03;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	byOutputBuffer[dwNumBytesToSend++] = 0x03;
	byOutputBuffer[dwNumBytesToSend++] = (dwAddress >> 16)	& 0xFF;
	byOutputBuffer[dwNumBytesToSend++] = (dwAddress >> 8)	& 0xFF;
	byOutputBuffer[dwNumBytesToSend++] = (dwAddress >> 0)	& 0xFF;


	byOutputBuffer[dwNumBytesToSend++] = 0x24;
	byOutputBuffer[dwNumBytesToSend++] = 0xFF;
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byGPIOs |= SS;
	byOutputBuffer[dwNumBytesToSend++] = 0x80;
	byOutputBuffer[dwNumBytesToSend++] = byGPIOs;
	byOutputBuffer[dwNumBytesToSend++] = byDirections;

	
	dwNumBytesToReceive = 0x100;

	ftStatus = FT_Write(ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent);
	if(ftStatus == FT_OK)
		while(dwNumBytesSent != dwNumBytesToSend);

	do
	{
		ftStatus = FT_GetQueueStatus(ftHandle, &dwNumBytesToRead);
	}while( (dwNumBytesToRead < dwNumBytesToReceive) &&  (ftStatus == FT_OK) );


	dwNumBytesRead = 0;
	ftStatus = FT_Read(ftHandle, pbyDest, dwNumBytesToReceive, &dwNumBytesRead);
	dwNumBytesToSend = 0;
	dwNumBytesToReceive = 0;
//	memcpy(pbyDest, byInputBuffer, 3);
}


int _tmain(int argc, _TCHAR* argv[])
{
	FT_HANDLE l_ftHandle;
	UINT32 kekeke = 0;
	DWORD dwValue = 0;
	if(SPI_InitDevice(&l_ftHandle) != FT_OK)
		return -1;

	//PBYTE temp = (PBYTE) malloc(0x100);
	//for(int i=0; i<0x100; i++)
	//	temp[i] = (BYTE) i;

//	EEPROM_EraseBulk(l_ftHandle);
//	EEPROM_ProgramPage(l_ftHandle, 0, temp);

	//memset(temp, 0, 0x100);
	//EEPROM_ReadPage(l_ftHandle, 0, temp);
	//free(temp);

	SPI_InitSMC(l_ftHandle);

	SPI_ReadRegister(l_ftHandle, 0, (LPDWORD) &kekeke);
	printf("\nFlashConfig: 0x%0X\n", kekeke);
//	CFile pFilea(_T("D:\\Xbox360\\deinemudder\\orig_backup.bin"), CFile::modeCreate | CFile::modeWrite);
//	if(!pFilea)
//	{
//		pFilea.Close();
//		return -1;
//	}
//
//	CFile pFile(_T("D:\\Xbox360\\kekeke\\XBR_Xenon_8955_3.bin"), CFile::modeRead);
//	if(!pFile)
//	{
//		pFile.Close();
//		return -1;
//	}
//
//	PUCHAR pbyBuffer = (PUCHAR) malloc(0x4200);
//	PUCHAR ptemp = pbyBuffer;
//	pFile.SeekToBegin();
//	pFilea.SeekToBegin();
//
	ULONG temp, totaltime = 0;
	DWORD i;
//	DWORD config = 0;
//	DWORD status = 0;
//
//


	for(i=0; i<0x100; i+=4)
	{
		SPI_ReadRegister(l_ftHandle, i, &temp);
		printf("\nRegister %i: 0x%X", i>>2, temp);
	}

//	for(i=0; i<=0x3FF; i++)
//	{
//
//		//SPI_WriteRegister(l_ftHandle, 0x0c, i << 14);
//		//SPI_ReadRegister(l_ftHandle, 0x18, &temp);
//		//printf("\nLogical: 0x%X; Physical: 0x%X", (i<<14), temp);
//
//		
//		ReadPage(l_ftHandle, i << 5, pbyBuffer);
//		SPI_ReadRegister(l_ftHandle, 0x18, &temp);
//		printf("\nErasing Address 0x%X", temp);
//		EraseBlock(l_ftHandle, temp);
//		
//
//		pFile.Read(pbyBuffer, 0x4200);
//		ptemp = pbyBuffer;
//		for(WORD j=0; j<=0x1F; j++)
//		{
//			WritePage(l_ftHandle, (i << 5) + j, ptemp);
//			ptemp += 0x210;
//		}
//		
//
//		
//		temp = GetTickCount();
//		ptemp = pbyBuffer;
//		for(WORD j=0; j<=0x1F; j++)
//		{
//			memset(ptemp, 0, 0x210);
//			ReadPage(l_ftHandle, (i << 5) + j, ptemp);
//			ptemp += 0x210;
//		}
//
//		temp = GetTickCount() - temp;
//		totaltime += temp;
//
//		pFilea.Write(pbyBuffer, 0x4200);
//
//		printf("\nBlock 0x%X took %i ms to read", i, temp);
//	}
//	printf("\nTotal time: %d", totaltime);
//
//	pFile.Close();
//	pFilea.Close();
	SPI_DeInitSMC(l_ftHandle);
//
////	printf("\nInitializing DMA");
////	InitDMA(l_ftHandle);
//
	SPI_Close(l_ftHandle);
	getchar();
	//free(pbyBuffer);
}