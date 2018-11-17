/*
 * Amazon FreeRTOS MQTT Echo Demo V1.2.6
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */


/**
 * @file uzed_iot.c
 * @brief A simple MQTT sensor example for the MicroZed IOT Kit.
 *
 * It creates an MQTT client that periodically publishes sensor readings to
 * MQTT topics at a defined rate.
 *
 * The demo uses one task. The task implemented by
 * prvMQTTConnectAndPublishTask() creates the MQTT client, subscribes to the
 * broker specified by the clientcredentialMQTT_BROKER_ENDPOINT constant,
 * performs the publish operations periodically forever.
 */

//////////////////// USER PARAMETERS ////////////////////
/* Sampling period, in ms. Two messages per period: pressure and temperature */
#define SAMPLING_PERIOD_MS		500

/* Timeout used when establishing a connection, which required TLS
* negotiation. */
#define democonfigMQTT_UZED_TLS_NEGOTIATION_TIMEOUT        pdMS_TO_TICKS( 12000 )

/**
 * @brief MQTT client ID.
 *
 * It must be unique per MQTT broker.
 */
#define UZedCLIENT_ID          ( ( const uint8_t * ) "MQTTUZed" )

/**
 * @brief Dimension of the character array buffers used to hold data (strings in
 * this case) that is published to and received from the MQTT broker (in the cloud).
 */
#define UZedMAX_DATA_LENGTH    256

/**
 * @brief A block time of 0 simply means "don't block".
 */
#define UZedDONT_BLOCK         ( ( TickType_t ) 0 )

//////////////////// END USER PARAMETERS ////////////////////

#if SAMPLING_PERIOD_MS < 100
#error Sampling period must be at least 100 ms
#endif

/*-----------------------------------------------------------*/

/* Standard includes. */
#include "string.h"
#include "stdio.h"
#include <stdarg.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "message_buffer.h"

/* MQTT includes. */
#include "aws_mqtt_agent.h"

/* Credentials includes. */
#include "aws_clientcredential.h"

/* Demo includes. */
#include "aws_demo_config.h"
#include "xil_types.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xiic.h"
#include "xgpiops.h"
#include "xspi_l.h"
#include "uzed_iot.h"

/*-----------------------------------------------------------*/
// System parameters for the MicroZed IOT kit

/**
 * @brief This is the LPS25HB on the Arduino shield board
 */
#define BAROMETER_SLAVE_ADDRESS		0x5D
/**
 * @brief This is the HTS221 on the Arduino shield board
 */
#define HYGROMETER_SLAVE_ADDRESS	0x5F

/**
 * @brief LED pin represents connection state
 */
#define LED_PIN	47

/**
 * @brief Barometer register defines
 */
#define BAROMETER_REG_REF_P_XL			0x08
#define BAROMETER_REG_REF_P_L			0x09
#define BAROMETER_REG_REF_P_H			0x0A
#define BAROMETER_REG_WHO_AM_I			0x0F
#define BAROMETER_REG_RES_CONF			0x10

#define BAROMETER_REG_CTRL_REG1			0x20
#define BAROMETER_BFLD_PD				(1<<7)

#define BAROMETER_REG_CTRL_REG2			0x21
#define BAROMETER_BFLD_BOOT				(1<<7)
#define BAROMETER_BFLD_SWRESET			(1<<2)
#define BAROMETER_BFLD_ONE_SHOT			(1<<0)

#define BAROMETER_REG_CTRL_REG3			0x22
#define BAROMETER_REG_CTRL_REG4			0x23
#define BAROMETER_REG_INTERRUPT_CFG		0x24
#define BAROMETER_REG_INT_SOURCE		0x25

#define BAROMETER_REG_STATUS_REG		0x27
#define BAROMETER_BFLD_P_DA				(1<<1)
#define BAROMETER_BFLD_T_DA				(1<<0)

#define BAROMETER_REG_PRESS_OUT_XL		0x28
#define BAROMETER_REG_PRESS_OUT_L		0x29
#define BAROMETER_REG_PRESS_OUT_H		0x2A
#define BAROMETER_REG_TEMP_OUT_L		0x2B
#define BAROMETER_REG_TEMP_OUT_H		0x2C
#define BAROMETER_REG_FIFO_CTRL			0x2E
#define BAROMETER_REG_FIFO_STATUS		0x2F
#define BAROMETER_REG_THS_P_L			0x30
#define BAROMETER_REG_THS_P_H			0x31
#define BAROMETER_REG_RPDS_L			0x39
#define BAROMETER_REG_RPDS_H			0x3A

/**
 * @brief Hygrometer register defines
 */
#define HYGROMETER_REG_WHO_AM_I			0x0F
#define HYGROMETER_REG_AV_CONF			0x10

#define HYGROMETER_REG_CTRL_REG1		0x20
#define HYGROMETER_BFLD_PD				(1<<7)

#define HYGROMETER_REG_CTRL_REG2		0x21
#define HYGROMETER_BFLD_BOOT			(1<<7)
#define HYGROMETER_BFLD_ONE_SHOT		(1<<0)

#define HYGROMETER_REG_CTRL_REG3		0x22

#define HYGROMETER_REG_STATUS_REG		0x27
#define HYGROMETER_BFLD_H_DA			(1<<1)
#define HYGROMETER_BFLD_T_DA			(1<<0)

#define HYGROMETER_REG_HUMIDITY_OUT_L	0x28
#define HYGROMETER_REG_HUMIDITY_OUT_H	0x29
#define HYGROMETER_REG_TEMP_OUT_L		0x2A
#define HYGROMETER_REG_TEMP_OUT_H		0x2B

#define HYGROMETER_REG_CALIB_0			0x30	// Convenience define for beginning of calibration registers
#define HYGROMETER_REG_H0_rH_x2			0x30
#define HYGROMETER_REG_H1_rH_x2			0x31
#define HYGROMETER_REG_T0_degC_x8		0x32
#define HYGROMETER_REG_T1_degC_x8		0x33
#define HYGROMETER_REG_T1_T0_MSB		0x35
#define HYGROMETER_REG_H0_T0_OUT_LSB	0x36
#define HYGROMETER_REG_H0_T0_OUT_MSB	0x37
#define HYGROMETER_REG_H1_T0_OUT_LSB	0x3A
#define HYGROMETER_REG_H1_T0_OUT_MSB	0x3B
#define HYGROMETER_REG_T0_OUT_LSB		0x3C
#define HYGROMETER_REG_T0_OUT_MSB		0x3D
#define HYGROMETER_REG_T1_OUT_LSB		0x3E
#define HYGROMETER_REG_T1_OUT_MSB		0x3F

/**
 * @brief AXI QSPI Temperature sensor defines
 */
#define PL_SPI_BASEADDR			XPAR_AXI_QUAD_SPI_0_BASEADDR  // Base address for AXI SPI controller

#define PL_SPI_CHANNEL_SEL_0		0xFFFFFFFE					// Select spi channel 0
#define PL_SPI_CHANNEL_SEL_1		0xFFFFFFFD					// Select spi channel 1
#define PL_SPI_CHANNEL_SEL_NONE		0xFFFFFFFF					// Deselect all SPI channels

// Initialization settings for the AXI SPI controller's Control Register when addressing the MAX31855
// 0x186 = b1_1000_0110
//			1	Inhibited to hold off transactions starting
//			1	Manually select the slave
//			0	Do not reset the receive FIFO at this time
//			0	Do not reset the transmit FIFO at this time
//			0	Clock phase of 0
//			0	Clock polarity of low
//			1	Enable master mode
//			1	Enable the SPI Controller
//			0	Do not put in loopback mode

#define MAX31855_CLOCK_PHASE_CPHA		0
#define MAX31855_CLOCK_POLARITY_CPOL	0

#define MAX31855_CR_INIT_MODE		XSP_CR_TRANS_INHIBIT_MASK | XSP_CR_MANUAL_SS_MASK   | \
									XSP_CR_MASTER_MODE_MASK   | XSP_CR_ENABLE_MASK
#define MAX31855_CR_UNINHIBIT_MODE	                            XSP_CR_MANUAL_SS_MASK   | \
									XSP_CR_MASTER_MODE_MASK   | XSP_CR_ENABLE_MASK
#define AXI_SPI_RESET_VALUE			0x0A  //!< Reset value for the AXI SPI Controller

/**
 * @brief Utility macro to uniformly process errors
 */
#define MAY_DIE(code)	\
	{ \
	    code; \
		if(pSystem->rc != XST_SUCCESS) { \
			prvPublishTopic(pSystem,pSystem->eTopic,pSystem->pcErr,pSystem->rc); \
			StopHere(); \
			goto L_DIE; \
		} \
	}

static inline BaseType_t MS_TO_TICKS(BaseType_t xMs)
{
	TickType_t xTicks = pdMS_TO_TICKS( xMs );

	if(xTicks < 1) {
		xTicks = 1;
	}
	return xTicks;
}

/*-----------------------------------------------------------*/

/**
 * @brief The topics that the MQTT client publishes
 */
typedef enum {
	TOPIC_BAROMETER_PRESSURE,
	TOPIC_BAROMETER_TEMPERATURE,
	TOPIC_BAROMETER_STATUS,
	TOPIC_THERMOCOUPLE_TEMPERATURE,
	TOPIC_THERMOCOUPLE_BOARD_TEMPERATURE,
	TOPIC_THERMOCOUPLE_STATUS,
	TOPIC_HYGROMETER_RELATIVE_HUMIDITY,
	TOPIC_HYGROMETER_HUMIDITY_SENSOR_TEMPERATURE,
	TOPIC_HYGROMETER_STATUS,
	TOPIC_SYSTEM_STATUS,
	TOPIC_LAST
} TOPIC;

typedef struct TopicInfo {
	TOPIC eTopic;
	const uint8_t* pbName;
	uint16_t usNameLen;
} TopicInfo;

/**
 * @brief System handle contents
 */
typedef struct System {
	XIic 	iic;
	XGpioPs gpio;

	MQTTAgentHandle_t xMQTTHandle;

	u8 pbHygrometerCalibration[16];

	int rc;
	const char* pcErr;
	TOPIC eTopic;
} System;

/*-----------------------------------------------------------*/

static TopicInfo pTopicInfo[] = {
		{TOPIC_BAROMETER_PRESSURE,						(const uint8_t*)"/remote_io_module/sensor_value/Pressure",				0},
		{TOPIC_BAROMETER_TEMPERATURE,					(const uint8_t*)"/remote_io_module/sensor_value/Pressure_Sensor_Temp",	0},
		{TOPIC_BAROMETER_STATUS,						(const uint8_t*)"/remote_io_module/sensor_status/LPS25HB_Error",		0},
		{TOPIC_THERMOCOUPLE_TEMPERATURE,				(const uint8_t*)"/remote_io_module/sensor_value/Thermocouple_Temp",		0},
		{TOPIC_THERMOCOUPLE_BOARD_TEMPERATURE,			(const uint8_t*)"/remote_io_module/sensor_value/Board_Temp_1",			0},
		{TOPIC_THERMOCOUPLE_STATUS,						(const uint8_t*)"/remote_io_module/sensor_status/MAX31855_Error",		0},
		{TOPIC_HYGROMETER_RELATIVE_HUMIDITY,			(const uint8_t*)"/remote_io_module/sensor_value/Relative_Humidity",		0},
		{TOPIC_HYGROMETER_HUMIDITY_SENSOR_TEMPERATURE,	(const uint8_t*)"/remote_io_module/sensor_value/Humidity_Sensor_Temp",	0},
		{TOPIC_HYGROMETER_STATUS,						(const uint8_t*)"/remote_io_module/sensor_status/HTS221_Error",			0},
		{TOPIC_SYSTEM_STATUS,							(const uint8_t*)"/remote_io_module/sensor_status/System_Error",			0},
		{TOPIC_LAST,									(const uint8_t*)NULL,													0}
};

/**
 * Semaphore to control Iic interface
 */
SemaphoreHandle_t xIicSemaphore;

/*-----------------------------------------------------------*/
/**
 * @brief Convenience function for breakpoints
 */
static void StopHere(void);

/*-----------------------------------------------------------*/

/**
 * @brief Initializes topic info data structure
 */
static void InitTopicInfo(void);

/**
 * @brief Publishes on any topic with caller-specified printf-style format for data
 *
 * @param[in] pSystem	System info
 * @param[in] eTopic	The topic (enum) to publish
 * @param[in] pcFmt		Printf format string to interpret variable arguments that follow
 */
static void prvPublishTopic( System* pSystem, TOPIC eTopic, const char* pcFmt, ... );

/**
 * @brief Creates an MQTT client and then connects to the MQTT broker.
 *
 * The MQTT broker end point is set by clientcredentialMQTT_BROKER_ENDPOINT.
 *
 * @return Exit task if failure
 */
static void prvCreateClientAndConnectToBroker( System* pSystem );

/*-----------------------------------------------------------*/

/**
 * @brief Starts complete system
 *
 * @return Exit task if failure
 */
static void StartSystem(System* pSystem);

/**
 * @brief Stops complete system
 *
 * @return Exit task
 */
static void StopSystem(System* pSystem);

/**
 * @brief Implements the task that connects to and then publishes messages to the
 * MQTT broker.
 *
 * Messages are published at 2Hz for a minute.
 *
 * @param[in] pvParameters Parameters passed while creating the task. Unused in our
 * case.
 */
static void prvMQTTConnectAndPublishTask( void * pvParameters );

/*-----------------------------------------------------------*/

/**
 * @brief Blink system LED
 *
 * @param[in] pSystem	System handle
 * @param[in] xCount	Number of times to blink LED
 * @param[in] xFinalOn	Should the LED be left on or off at end
 */
static void BlinkLed(System* pSystem,BaseType_t xCount, BaseType_t xFinalOn);

/*-----------------------------------------------------------*/

/**
 * @brief Read multiple IIC registers
 *
 * @param[in] pSystem			System handle
 * @param[in] bSlaveAddress		Slave address on bus
 * @param[in] xCount			Number of registers to read
 * @param[in] bFirstSlaveReg	First register number on device
 * @param[in] pbBuf				Byte buffer to deposit data read from device
 */
static int ReadIicRegs(System* pSystem,u8 bSlaveAddress,BaseType_t xCount,u8 bFirstSlaveReg,u8* pbBuf);

/**
 * @brief Read single IIC register
 *
 * @param[in] pSystem		System handle
 * @param[in] bSlaveAddress	Slave address on bus
 * @param[in] bSlaveReg		Register number on slave device
 * @param[in] pbBuf			Byte buffer to deposit data read from device
 */
static int ReadIicReg(System* pSystem,u8 bSlaveAddress,u8 bSlaveReg,u8* pbBuf);

/**
 * @brief Write multiple IIC registers
 *
 * @param[in] pSystem		System handle
 * @param[in] bSlaveAddress	Slave address on bus
 * @param[in] xCount		Number of registers to write
 * @param[in] pbBuf			Byte buffer with data to write - >= 2 bytes - first byte is always register number on slave device
 */
static int WriteIicRegs(System* pSystem,u8 bSlaveAddress,BaseType_t xCount,u8* pbBuf);

/**
 * @brief Write single IIC register
 *
 * @param[in] pSystem	System handle
 * @param[in] bSlaveAddress	Slave address on bus
 * @param[in] bSlaveReg	Register number on slave device
 * @param[in] pbBuf		Byte buffer with data to write - 2 bytes - first byte is always register number on slave device
 */
static int WriteIicReg(System* pSystem,u8 bSlaveAddress,u8 bSlaveReg,u8 bVal);

/*-----------------------------------------------------------*/

/**
 * @brief Start Barometer
 *
 * @param[in] pSystem	System handle
 */
static void StartBarometer(System* pSystem);

/**
 * @brief Stop Barometer
 *
 * @param[in] pSystem	System handle
 */
static void StopBarometer(System* pSystem);


/**
 * @brief Sample Barometer and publish values
 *
 * @param[in] pSystem			System handle
 */
static void SampleBarometer(System* pSystem);

/*-----------------------------------------------------------*/

/**
 * @brief Start Hygrometer
 *
 * @param[in] pSystem	System handle
 */
static void StartHygrometer(System* pSystem);

/**
 * @brief Stop Hygrometer
 *
 * @param[in] pSystem	System handle
 */
static void StopHygrometer(System* pSystem);


/**
 * @brief Sample Hygrometer and publish values
 *
 * @param[in] pSystem			System handle
 */
static void SampleHygrometer(System* pSystem);

/*-----------------------------------------------------------*/

/**
 * @brief Start PL Temperature Sensor
 *
 * @param[in] pSystem	System handle
 */
static void StartPLTempSensor(System* pSystem);

/**
 * @brief Stop PL Temperature Sensor
 *
 * @param[in] pSystem	System handle
 */
static void StopPLTempSensor(System* pSystem);

/**
 * @brief PL Temperature Sensor: utility function to do SPI transaction
 *
 * @param[in] 	pSystem			System handle
 * @param[in] 	qBaseAddress	AXI SPI Controller Base Address
 * @param[in] 	xSPI_Channel	SPI Channel to use
 * @param[in] 	xByteCount		Number of bytes to transfer
 * @param[in] 	pqTxBuffer		Data to send
 * @param[out] 	pqRxBuffer		Data to receive
 */
static void XSpi_LowLevelExecute(System* pSystem, u32 qBaseAddress, BaseType_t xSPI_Channel, BaseType_t xByteCount, const u32* pqTxBuffer, u32* pqRxBuffer);

/**
 * @brief Sample Barometer and publish values
 *
 * @param[in] pSystem			System handle
 */
static void SamplePLTempSensor(System* pSystem);


/*--------------------------------------------------------------------------------*/
static void StopHere(void)
{
	;
}

/*--------------------------------------------------------------------------------*/

static void BlinkLed(System* pSystem,BaseType_t xCount, BaseType_t xFinalOn)
{
	BaseType_t x;
	const TickType_t xHalfSecond = MS_TO_TICKS( 500 );

	if(!pSystem->gpio.IsReady) {
		return;
	}
	for(x = 0; x < xCount; x++) {
		XGpioPs_WritePin(&pSystem->gpio, LED_PIN, 1);
		vTaskDelay(xHalfSecond);

		XGpioPs_WritePin(&pSystem->gpio, LED_PIN, 0);
		vTaskDelay(xHalfSecond);
	}
	if(xFinalOn) {
		XGpioPs_WritePin(&pSystem->gpio, LED_PIN, 1);
	}
}

/*-----------------------------------------------------------*/

static void InitTopicInfo(void)
{
	TopicInfo* pTI;

	for(pTI = pTopicInfo; TOPIC_LAST != pTI->eTopic; pTI++) {
		pTI->usNameLen = strlen((const char*)(pTI->pbName));
	}
}

static const TopicInfo* GetTopicInfo(TOPIC eTopic)
{
	TopicInfo* pTI;

	for(pTI = pTopicInfo; TOPIC_LAST != pTI->eTopic; pTI++) {
		if(pTI->eTopic == eTopic) {
			return (const TopicInfo*)pTI;
		}
	}

	configASSERT( pdTRUE );
	/* Not reached */
	return (const TopicInfo*)NULL;
}

static void prvCreateClientAndConnectToBroker( System* pSystem )
{
    MQTTAgentConnectParams_t xConnectParameters =
    {
        clientcredentialMQTT_BROKER_ENDPOINT, /* The URL of the MQTT broker to connect to. */
        democonfigMQTT_AGENT_CONNECT_FLAGS,   /* Connection flags. */
        pdFALSE,                              /* Deprecated. */
        clientcredentialMQTT_BROKER_PORT,     /* Port number on which the MQTT broker is listening. Can be overridden by ALPN connection flag. */
        UZedCLIENT_ID,                        /* Client Identifier of the MQTT client. It should be unique per broker. */
        0,                                    /* The length of the client Id, filled in later as not const. */
        pdFALSE,                              /* Deprecated. */
        NULL,                                 /* User data supplied to the callback. Can be NULL. */
        NULL,                                 /* Callback used to report various events. Can be NULL. */
        NULL,                                 /* Certificate used for secure connection. Can be NULL. */
        0                                     /* Size of certificate used for secure connection. */
    };

    /* The MQTT client object must be created before it can be used.  The
     * maximum number of MQTT client objects that can exist simultaneously
     * is set by mqttconfigMAX_BROKERS. */
    if( eMQTTAgentSuccess == MQTT_AGENT_Create( &pSystem->xMQTTHandle ) ) {
        /* Fill in the MQTTAgentConnectParams_t member that is not const,
         * and therefore could not be set in the initializer (where
         * xConnectParameters is declared in this function). */
        xConnectParameters.usClientIdLength = ( uint16_t ) strlen( ( const char * ) UZedCLIENT_ID );

        /* Connect to the broker. */
        configPRINTF( ( "INFO: MQTT UZed attempting to connect to %s.\r\n", clientcredentialMQTT_BROKER_ENDPOINT ) );

        if( eMQTTAgentSuccess ==
        		MQTT_AGENT_Connect( pSystem->xMQTTHandle, &xConnectParameters, democonfigMQTT_UZED_TLS_NEGOTIATION_TIMEOUT ) ) {
            configPRINTF( ( "SUCCESS: MQTT UZed connected.\r\n" ) );
            pSystem->rc = XST_SUCCESS;
        } else {
            /* Could not connect, so delete the MQTT client. */
            ( void ) MQTT_AGENT_Delete( pSystem->xMQTTHandle );
            configPRINTF( ( "ERROR:  MQTT UZed failed to connect\r\n" ) );
            pSystem->rc = XST_FAILURE;
        	pSystem->pcErr = "Could not connect to MQTT Agent";
            pSystem->xMQTTHandle = NULL;
        }
    } else {
    	configPRINTF( ( "ERROR:  Could not create MQTT Agent\r\n" ) );
        pSystem->rc = XST_FAILURE;
    	pSystem->pcErr = "Could not create MQTT Agent";
    	pSystem->xMQTTHandle = NULL;
    }
}

static void prvPublishTopic( System* pSystem, TOPIC eTopic, const char* pcFmt, ... )
{
    MQTTAgentPublishParams_t xPublishParameters;
    MQTTAgentReturnCode_t xReturned;
    char pcDataBuffer[ UZedMAX_DATA_LENGTH ];
    int iDataLength;
    const TopicInfo* pTI;
    va_list ap;

    if(pSystem->xMQTTHandle == NULL) {
    	return;
    }

    /*
     * Compose the message
     */
    va_start(ap,pcFmt);
    iDataLength = vsnprintf(pcDataBuffer, UZedMAX_DATA_LENGTH, pcFmt, ap);
    va_end(ap);
    pcDataBuffer[UZedMAX_DATA_LENGTH - 1] = 0;	// safety
    if(iDataLength >= UZedMAX_DATA_LENGTH) {
    	iDataLength = UZedMAX_DATA_LENGTH - 1;
    } else if(iDataLength < 0) {
    	iDataLength = 3;
    	pcDataBuffer[0] = '?';
    	pcDataBuffer[1] = '?';
    	pcDataBuffer[2] = '?';
    	pcDataBuffer[3] = '\0';
    }

    pTI = GetTopicInfo(eTopic);

    /* Setup the publish parameters. */
    memset( &( xPublishParameters ), 0, sizeof( xPublishParameters ) );
    xPublishParameters.pucTopic = pTI->pbName;
    xPublishParameters.pvData = (void*)pcDataBuffer;
    xPublishParameters.usTopicLength = pTI->usNameLen;
    xPublishParameters.ulDataLength = ( uint32_t ) iDataLength;
    xPublishParameters.xQoS = eMQTTQoS1;

    /* Publish the message. */
    xReturned = MQTT_AGENT_Publish( pSystem->xMQTTHandle, &xPublishParameters, democonfigMQTT_TIMEOUT );
    switch(xReturned) {
    case eMQTTAgentSuccess:
    	configPRINTF( ( "Success: UZed published '%s': '%s'\r\n", (const char*)pTI->pbName, pcDataBuffer ) );
    	break;

    case eMQTTAgentFailure:
    	BlinkLed(pSystem, 1, pdFALSE);
        configPRINTF( ( "ERROR:  UZed failed to publish '%s': '%s'\r\n", (const char*)pTI->pbName, pcDataBuffer ) );
        break;

    case eMQTTAgentTimeout:
    	BlinkLed(pSystem, 1, pdFALSE);
        configPRINTF( ( "ERROR:  UZed timed out to publish '%s': '%s'\r\n", (const char*)pTI->pbName, pcDataBuffer ) );
        break;

    default:	//FallThrough
    case eMQTTAgentAPICalledFromCallback:
    	BlinkLed(pSystem, 1, pdFALSE);
        configPRINTF( ( "ERROR:  UZed unexpected callback to publish '%s': '%s'\r\n", (const char*)pTI->pbName, pcDataBuffer ) );
    	configASSERT(pdTRUE);
    	break;	// Not reached
    }
}

/*--------------------------------------------------------------------------------*/

static int ReadIicRegs(System* pSystem,u8 bSlaveAddress,BaseType_t xCount,u8 bFirstSlaveReg,u8* pbBuf)
{
	BaseType_t xReceived;

	if(xCount > 1) {
		bFirstSlaveReg |= 0x80;
	}

	portENTER_CRITICAL();

	MAY_DIE({
		xSemaphoreTake(xIicSemaphore, portMAX_DELAY);
		if(1 != XIic_Send(pSystem->iic.BaseAddress,bSlaveAddress,&bFirstSlaveReg,1,XIIC_REPEATED_START)) {
			pSystem->rc = 1;
			pSystem->pcErr = "ReadIicRegs::XIic_Send(Addr) -> %08x";
		}
		xSemaphoreGive(xIicSemaphore);
	});
	MAY_DIE({
		xSemaphoreTake(xIicSemaphore, portMAX_DELAY);
		xReceived = XIic_Recv(pSystem->iic.BaseAddress,bSlaveAddress,pbBuf,xCount,XIIC_STOP);
		if(xCount != xReceived) {
			pSystem->rc = xReceived;
			pSystem->pcErr = "ReadIicRegs::XIic_Recv(Data) -> %08x";
		}
		xSemaphoreGive(xIicSemaphore);
	});

	portEXIT_CRITICAL();

L_DIE:
	return pSystem->rc;
}

static int ReadIicReg(System* pSystem, u8 bSlaveAddress, u8 bFirstSlaveReg, u8* pbBuf)
{
	return ReadIicRegs(pSystem, bSlaveAddress, 1, bFirstSlaveReg, pbBuf);
}

static int WriteIicRegs(System* pSystem, u8 bSlaveAddress, BaseType_t xCount, u8* pbBuf)
{
	BaseType_t xSent;

	if(xCount > 2) {
		pbBuf[0] |= 0x80;
	}

	MAY_DIE({
		xSemaphoreTake(xIicSemaphore, portMAX_DELAY);
		xSent = XIic_Send(pSystem->iic.BaseAddress,bSlaveAddress,pbBuf,xCount,XIIC_STOP);
		if(xCount != xSent) {
			pSystem->rc = xSent;
			pSystem->pcErr = "WriteIicRegs::XIic_Send(Buf) -> %08x";
		}
		xSemaphoreGive(xIicSemaphore);
	});

L_DIE:
	return pSystem->rc;
}

static int WriteIicReg(System* pSystem,u8 bSlaveAddress, u8 bFirstSlaveReg, u8 bVal)
{
	u8 pbBuf[2];

	pbBuf[0] = bFirstSlaveReg;
	pbBuf[1] = bVal;
	return WriteIicRegs(pSystem, bSlaveAddress, 2, pbBuf);
}

/*--------------------------------------------------------------------------------*/

static void StartBarometer(System* pSystem)
{
	u8 b;
	int iTimeout;
	TickType_t xOneMs = MS_TO_TICKS( 1 );

	pSystem->eTopic = TOPIC_BAROMETER_STATUS;

	// Verify it is the right chip
	MAY_DIE({
		ReadIicReg(pSystem,BAROMETER_SLAVE_ADDRESS,BAROMETER_REG_WHO_AM_I,&b);
		pSystem->pcErr = "ReadIicReg(WHO_AM_I) -> %08x";
	});
	MAY_DIE({
		if(0xBD != b) {
			pSystem->rc = b;
			pSystem->pcErr = "BAROMETER_WHO_AM_I = %08x != BD";
		}
	});

	// Reset chip: first swreset, then boot
	MAY_DIE({
		WriteIicReg(pSystem,BAROMETER_SLAVE_ADDRESS,BAROMETER_REG_CTRL_REG2,BAROMETER_BFLD_SWRESET);
		pSystem->pcErr = "WriteIicReg(BAROMETER_REG_CTRL_REG2::BFLD_SWRESET) -> %08x";
	});
	for(iTimeout = 100; iTimeout-- > 0; ) {
		MAY_DIE({
			ReadIicReg(pSystem,BAROMETER_SLAVE_ADDRESS,BAROMETER_REG_CTRL_REG2,&b);
			pSystem->pcErr = "ReadIicReg(BAROMETER_REG_CTRL_REG2) -> %08x";
		});
		if(0 == (b & BAROMETER_BFLD_SWRESET)) {
			break;
		}
		vTaskDelay(xOneMs);
	}
	if(iTimeout <= 0) {
		MAY_DIE({
			pSystem->rc = XST_FAILURE;
			pSystem->pcErr = "Barometer swreset timeout";
		});
	}

	MAY_DIE({
		WriteIicReg(pSystem,BAROMETER_SLAVE_ADDRESS,BAROMETER_REG_CTRL_REG2,BAROMETER_BFLD_BOOT);
		pSystem->pcErr = "WriteIicReg(BAROMETER_REG_CTRL_REG2::BAROMETER_BFLD_BOOT -> %08x";
	});
	for(iTimeout = 100; iTimeout-- > 0; ) {
		MAY_DIE({
			ReadIicReg(pSystem,BAROMETER_SLAVE_ADDRESS,BAROMETER_REG_CTRL_REG2,&b);
			pSystem->pcErr = "ReadIicReg(BAROMETER_REG_CTRL_REG2) -> %08x";
		});
		if(0 == (b & BAROMETER_BFLD_BOOT)) {
			break;
		}
		vTaskDelay(xOneMs);
	}
	if(iTimeout <= 0) {
		MAY_DIE({
			pSystem->pcErr = "Barometer boot timeout";
		});
	}

	MAY_DIE({
		WriteIicReg(pSystem,BAROMETER_SLAVE_ADDRESS,BAROMETER_REG_CTRL_REG1,BAROMETER_BFLD_PD);
		pSystem->pcErr = "WriteIicReg(BAROMETER_REG_CTRL_REG1::BAROMETER_BFLD_PD) -> %08x";
	});
	vTaskDelay(xOneMs);

	prvPublishTopic(pSystem, TOPIC_BAROMETER_STATUS, "Barometer started");

L_DIE:
	return;
}

static void StopBarometer(System* pSystem)
{
	pSystem->eTopic = TOPIC_BAROMETER_STATUS;
}

static void SampleBarometer(System* pSystem)
{
	BaseType_t xTimeout;
	u8 b;
	u8 pbBuf[6];
	s32 sqTmp;
	float f;
	TickType_t xOneMs = MS_TO_TICKS( 1 );

	pSystem->eTopic = TOPIC_BAROMETER_STATUS;

	/*
	 * NOTE: The one shot auto clears but it seems to take 36ms
	 * Our sampling period is >= 100ms so the one shot will auto clear by the next sample time
	 */
	MAY_DIE({
		WriteIicReg(pSystem,BAROMETER_SLAVE_ADDRESS,BAROMETER_REG_CTRL_REG2,BAROMETER_BFLD_ONE_SHOT);
		pSystem->pcErr = "WriteIicReg(BAROMETER_REG_CTRL_REG2::BAROMETER_BFLD_ONE_SHOT) -> %08x";
	});
	for(xTimeout = 50; xTimeout-- > 0; ) {
		MAY_DIE({
			ReadIicReg(pSystem,BAROMETER_SLAVE_ADDRESS,BAROMETER_REG_CTRL_REG2,&b);
			pSystem->pcErr = "ReadIicRegs(6@BAROMETER_REG_STATUS_REG) -> %08x";
		});
		if(0 == (b & BAROMETER_BFLD_ONE_SHOT)) {
			break;
		}
		vTaskDelay(xOneMs);
	}
	MAY_DIE({
		if(xTimeout <= 0) {
			pSystem->rc = XST_FAILURE;
			pSystem->pcErr = "Timed out waiting for BAROMETER_BFLD_ONE_SHOT";
		}
	});

	for(xTimeout = 50; xTimeout-- > 0; ) {
		MAY_DIE({
			ReadIicRegs(pSystem,BAROMETER_SLAVE_ADDRESS,6,BAROMETER_REG_STATUS_REG,pbBuf);
			pSystem->pcErr = "ReadIicRegs(6@BAROMETER_REG_STATUS_REG) -> %08x";
		});
		if((BAROMETER_BFLD_P_DA | BAROMETER_BFLD_T_DA) == (pbBuf[0] & (BAROMETER_BFLD_P_DA | BAROMETER_BFLD_T_DA))) {
			break;
		}
		vTaskDelay(xOneMs);
	}
	MAY_DIE({
		if(xTimeout <= 0) {
			pSystem->rc = XST_FAILURE;
			pSystem->pcErr = "Timed out waiting for P_DA and T_DA";
		}
	});

	// See ST TN1228
	sqTmp = 0
			| ((u32)pbBuf[1] << 0)	// xl
			| ((u32)pbBuf[2] << 8)	// l
			| ((u32)pbBuf[3] << 16)	// h
			;
	if(sqTmp & 0x00800000) {
		sqTmp |= 0xFF800000;
	}
	f = (float)sqTmp / 4096.0F;
	prvPublishTopic(pSystem, TOPIC_BAROMETER_PRESSURE, "%.2f hPa", f);

	sqTmp = 0
			| ((u32)pbBuf[4] << 0)	// l
			| ((u32)pbBuf[5] << 8)	// h
			;
	if(sqTmp & 0x00008000) {
		sqTmp |= 0xFFFF8000;
	}
	f = 42.5F + (float)sqTmp/480.0;
	prvPublishTopic(pSystem, TOPIC_BAROMETER_TEMPERATURE, "%.2f C", f);

L_DIE:
	return;
}

/*--------------------------------------------------------------------------------*/

static void StartHygrometer(System* pSystem)
{
	u8 b;
	int iTimeout;
	TickType_t xOneMs = MS_TO_TICKS( 1 );

	pSystem->eTopic = TOPIC_HYGROMETER_STATUS;

	// Verify it is the right chip
	MAY_DIE({
		ReadIicReg(pSystem,HYGROMETER_SLAVE_ADDRESS,HYGROMETER_REG_WHO_AM_I,&b);
		pSystem->pcErr = "ReadIicReg(HYGROMETER_WHO_AM_I) -> %08x";
	});
	MAY_DIE({
		if(0xBC != b) {
			pSystem->rc = b;
			pSystem->pcErr = "HYGROMETER_WHO_AM_I = %08x != BC";
		}
	});

	// Reset chip: boot
	MAY_DIE({
		WriteIicReg(pSystem,HYGROMETER_SLAVE_ADDRESS,HYGROMETER_REG_CTRL_REG2,HYGROMETER_BFLD_BOOT);
		pSystem->pcErr = "WriteIicReg(HYGROMETER_REG_CTRL_REG2::HYGROMETER_BFLD_BOOT -> %08x";
	});
	for(iTimeout = 1000; iTimeout-- > 0; ) {
		MAY_DIE({
			ReadIicReg(pSystem,HYGROMETER_SLAVE_ADDRESS,HYGROMETER_REG_CTRL_REG2,&b);
			pSystem->pcErr = "ReadIicReg(BAROMETER_REG_CTRL_REG2) -> %08x";
		});
		if(0 == (b & HYGROMETER_BFLD_BOOT)) {
			break;
		}
		vTaskDelay(xOneMs);
	}
	if(iTimeout <= 0) {
		MAY_DIE({
			pSystem->pcErr = "Hygrometer boot timeout";
		});
	}

	/*
	 * Read and store calibration values
	 */
	MAY_DIE({
		ReadIicRegs(pSystem,HYGROMETER_SLAVE_ADDRESS,16,HYGROMETER_REG_CALIB_0,&pSystem->pbHygrometerCalibration[0]);
		pSystem->pcErr = "ReadIicRegs(HYGROMETER_REG_CALIB_0) -> %08x";
	});

	/*
	 * Power up device
	 */
	MAY_DIE({
		WriteIicReg(pSystem,HYGROMETER_SLAVE_ADDRESS,HYGROMETER_REG_CTRL_REG1,HYGROMETER_BFLD_PD);
		pSystem->pcErr = "WriteIicReg(HYGROMETER_REG_CTRL_REG1::HYGROMETER_BFLD_PD) -> %08x";
	});
	vTaskDelay(xOneMs);

	prvPublishTopic(pSystem, TOPIC_HYGROMETER_STATUS, "Hygrometer started");

L_DIE:
	return;
}

static void StopHygrometer(System* pSystem)
{
	pSystem->eTopic = TOPIC_HYGROMETER_STATUS;
}

static void SampleHygrometer(System* pSystem)
{
	BaseType_t xTimeout;
	u8 b;
	u8 pbBuf[5];
	float f;
	TickType_t xOneMs = MS_TO_TICKS( 1 );

	pSystem->eTopic = TOPIC_HYGROMETER_STATUS;

	/*
	 * NOTE: The one shot auto clears but it seems to take FIXME ms
	 * Our sampling period is >= 100ms so the one shot will auto clear by the next sample time??? FIXME
	 */
	MAY_DIE({
		WriteIicReg(pSystem,HYGROMETER_SLAVE_ADDRESS,HYGROMETER_REG_CTRL_REG2,HYGROMETER_BFLD_ONE_SHOT);
		pSystem->pcErr = "WriteIicReg(HYGROMETER_REG_CTRL_REG2::HYGROMETER_BFLD_ONE_SHOT) -> %08x";
	});
	for(xTimeout = 10000; xTimeout-- > 0; ) {
		MAY_DIE({
			ReadIicReg(pSystem,HYGROMETER_SLAVE_ADDRESS,HYGROMETER_REG_CTRL_REG2,&b);
			pSystem->pcErr = "ReadIicRegs(6@HYGROMETER_REG_STATUS_REG) -> %08x";
		});
		if(0 == (b & HYGROMETER_BFLD_ONE_SHOT)) {
			break;
		}
		vTaskDelay(xOneMs);
	}
	MAY_DIE({
		if(xTimeout <= 0) {
			pSystem->rc = XST_FAILURE;
			pSystem->pcErr = "Timed out waiting for HYGROMETER_BFLD_ONE_SHOT";
		}
	});

	for(xTimeout = 50; xTimeout-- > 0; ) {
		MAY_DIE({
			ReadIicRegs(pSystem,HYGROMETER_SLAVE_ADDRESS,5,HYGROMETER_REG_STATUS_REG,pbBuf);
			pSystem->pcErr = "ReadIicRegs(6@HYGROMETER_REG_STATUS_REG) -> %08x";
		});
		if((HYGROMETER_BFLD_H_DA | HYGROMETER_BFLD_T_DA) == (pbBuf[0] & (HYGROMETER_BFLD_H_DA | HYGROMETER_BFLD_T_DA))) {
			break;
		}
		vTaskDelay(xOneMs);
	}
	MAY_DIE({
		if(xTimeout <= 0) {
			pSystem->rc = XST_FAILURE;
			pSystem->pcErr = "Timed out waiting for HYGROMETER P_DA and T_DA";
		}
	});

	/*
	 * TODO: Convert values read to actual numbers
	 * REF: ST TN1218
	 * Interpreting humidity and temperature readings in the HTS221 digital humidity sensor
	 */

	f = 100.0F;
	prvPublishTopic(pSystem, TOPIC_HYGROMETER_RELATIVE_HUMIDITY, "%.2f %%rH", f);

	f = 1000.0F;
	prvPublishTopic(pSystem, TOPIC_HYGROMETER_HUMIDITY_SENSOR_TEMPERATURE, "%.2f C", f);

L_DIE:
	return;
}

/*--------------------------------------------------------------------------------*/

/**
 * @brief Start Barometer
 *
 * @param[in] pSystem	System handle
 */
static void StartPLTempSensor(System* pSystem)
{
	const TickType_t xOneMs = MS_TO_TICKS(1);

	pSystem->eTopic = TOPIC_THERMOCOUPLE_STATUS;

	//Reset the SPI Peripheral, which takes 4 cycles, so wait a bit after reset
	XSpi_WriteReg(PL_SPI_BASEADDR, XSP_SRR_OFFSET, AXI_SPI_RESET_VALUE);
	vTaskDelay(xOneMs); //usleep(100);
	// Initialize the AXI SPI Controller with settings compatible with the MAX31855
	XSpi_WriteReg(PL_SPI_BASEADDR, XSP_CR_OFFSET, MAX31855_CR_INIT_MODE);
	// Deselect all slaves to start, then wait a bit for it to take affect
	XSpi_WriteReg(PL_SPI_BASEADDR, XSP_SSR_OFFSET, PL_SPI_CHANNEL_SEL_NONE);
	vTaskDelay(xOneMs); //usleep(100);

	prvPublishTopic(pSystem, TOPIC_THERMOCOUPLE_STATUS, "PL Thermocouple started");

}

/**
 * @brief Stop Barometer
 *
 * @param[in] pSystem	System handle
 */
static void StopPLTempSensor(System* pSystem)
{
	pSystem->eTopic = TOPIC_THERMOCOUPLE_STATUS;
}

static void XSpi_LowLevelExecute(System* pSystem, u32 qBaseAddress, BaseType_t xSPI_Channel, BaseType_t xByteCount, const u32* pqTxBuffer, u32* pqRxBuffer)
{
	BaseType_t xNumBytesRcvd = 0;
	BaseType_t xCount;
	const TickType_t xOneMs = MS_TO_TICKS(1);

	/*
	 * Initialize the Tx FIFO in the AXI SPI Controller with the transmit
	 * data contained in TxBuffer
	 */
	for (xCount = 0; xCount < xByteCount; pqTxBuffer++, xCount++)
	{
		XSpi_WriteReg(qBaseAddress, XSP_DTR_OFFSET, *pqTxBuffer);
	}

	// Assert the Slave Select, then wait a bit so it takes affect
	XSpi_WriteReg(qBaseAddress, XSP_SSR_OFFSET, xSPI_Channel);
	vTaskDelay(xOneMs); //usleep(100);

	/*
	 * Disable the Inhibit bit in the AXI SPI Controller's controler register
	 * This will release the AXI SPI Controller to release the transaction onto the bus
	 */
	XSpi_WriteReg(qBaseAddress, XSP_CR_OFFSET, MAX31855_CR_UNINHIBIT_MODE);

	/*
	 * Wait for the AXI SPI controller's transmit FIFO to transition to empty
	 * to make sure all the transmit data gets sent
	 */
	while (!(XSpi_ReadReg(qBaseAddress, XSP_SR_OFFSET) & XSP_SR_TX_EMPTY_MASK));

	/*
	 * Wait for the AXI SPI controller's Receive FIFO Occupancy register to
	 * show the expected number of receive bytes before attempting to read
	 * the Rx FIFO. Note the Occupancy Register shows Rx Bytes - 1
	 *
	 * If xByteCount number of bytes is sent, then by design, there must be
	 * xByteCount number of bytes received
	 */
	xByteCount--;
	while(xByteCount != XSpi_ReadReg(qBaseAddress, XSP_RFO_OFFSET)) {
		;
	}
	xByteCount++;

	/*
	 * The AXI SPI Controller's Rx FIFO has now received TxByteCount number
	 * of bytes off the SPI bus and is ready to be read.
	 *
	 * Transfer the Rx bytes out of the Controller's Rx FIFO into our code
	 * Keep reading one byte at a time until the Rx FIFO is empty
	 */
	xNumBytesRcvd = 0;
	while ((XSpi_ReadReg(qBaseAddress, XSP_SR_OFFSET) & XSP_SR_RX_EMPTY_MASK) == 0)
	{
		*pqRxBuffer++ = XSpi_ReadReg(qBaseAddress, XSP_DRR_OFFSET);
		xNumBytesRcvd++;
	}

	// Now that the Rx Data is retrieved, inhibit the AXI SPI Controller
	XSpi_WriteReg(qBaseAddress, XSP_CR_OFFSET, MAX31855_CR_INIT_MODE);
	// Deassert the Slave Select
	XSpi_WriteReg(qBaseAddress, XSP_SSR_OFFSET, PL_SPI_CHANNEL_SEL_NONE);

	/*
	 * If no data was sent or if we didn't receive as many bytes as
	 * were transmitted, then flag a failure
	 */
	if (xByteCount != xNumBytesRcvd) {
		pSystem->rc = XST_FAILURE;
		return;
	}

	pSystem->rc = XST_SUCCESS;
	return;
}

/**
 * @brief Sample Barometer and publish values
 *
 * @param[in] pSystem			System handle
 */
static void SamplePLTempSensor(System* pSystem)
{
	// TxBuffer is not used to communicate with the MAX31855 but it is still necessary
	//      for the XSPI utilities to function
	u32 pqTxBuffer[4] = {0,0,0,0};
	u32 pqRxBuffer[4] = {~0,~0,~0,~0};	// Initialize RxBuffer with all 1's
	s32 sqTemporaryValue = 0;
	s32 sqTemporaryValue2 = 0;
	float fMAX31855_internal_temp=0.0f;
	float fMAX31855_thermocouple_temp=0.0f;

	pSystem->eTopic = TOPIC_THERMOCOUPLE_STATUS;

	// Execute 4-byte read transaction.
	XSpi_LowLevelExecute(pSystem, (u32)PL_SPI_BASEADDR, (BaseType_t)PL_SPI_CHANNEL_SEL_0, (BaseType_t)4, pqTxBuffer, pqRxBuffer );

	// Check for various error codes
	if(0) {
		;
	} else if(XST_SUCCESS != pSystem->rc) {
		prvPublishTopic(pSystem, TOPIC_THERMOCOUPLE_STATUS, "SPI Transaction failure");
	} else if(pqRxBuffer[3] & 0x1) {
		prvPublishTopic(pSystem, TOPIC_THERMOCOUPLE_STATUS, "Open Circuit");
	} else if(pqRxBuffer[3] & 0x2) {
		prvPublishTopic(pSystem, TOPIC_THERMOCOUPLE_STATUS, "Short to GND");
	} else if(pqRxBuffer[3] & 0x4) {
		prvPublishTopic(pSystem, TOPIC_THERMOCOUPLE_STATUS, "Short to VCC");
	} else if(pqRxBuffer[1] & 0x01) {
		prvPublishTopic(pSystem, TOPIC_THERMOCOUPLE_STATUS, "Fault");
	} else {
		// Internal Temp
		{
			sqTemporaryValue = pqRxBuffer[2];  			// bits 11..4
			sqTemporaryValue = sqTemporaryValue << 4;		// shift left to make room for bits 3..0
			sqTemporaryValue2 = pqRxBuffer[3];				// bits 3..0 in the most significant spots
			sqTemporaryValue2 = sqTemporaryValue2 >> 4;	// shift right to get rid of extra bits and position
			sqTemporaryValue |= sqTemporaryValue2;		// Combine to get bits 11..0
			if((pqRxBuffer[2] & 0x80) == 0x80) {				// Check the sign bit and sign-extend if need be
				sqTemporaryValue |= 0xFFFFF800;
			}
			fMAX31855_internal_temp = (float)sqTemporaryValue / 16.0f;
			prvPublishTopic(pSystem, TOPIC_THERMOCOUPLE_BOARD_TEMPERATURE, "%.1f C", fMAX31855_internal_temp);
		}

		// Thermocouple Temp
		{
			sqTemporaryValue = pqRxBuffer[0];  			// bits 13..6
			sqTemporaryValue = sqTemporaryValue << 6;		// shift left to make room for bits 5..0
			sqTemporaryValue2 = pqRxBuffer[1];				// bits 5..0 in the most significant spots
			sqTemporaryValue2 = sqTemporaryValue2 >> 2;	// shift right to get rid of extra bits and position
			sqTemporaryValue |= sqTemporaryValue2;		// Combine to get bits 13..0
			if((pqRxBuffer[0] & 0x80) == 0x80) {				// Check the sign bit and sign-extend if need be
				sqTemporaryValue |= 0xFFFFE000;
			}
			fMAX31855_thermocouple_temp = (float)sqTemporaryValue / 4.0f;
			prvPublishTopic(pSystem, TOPIC_THERMOCOUPLE_TEMPERATURE, "%.1f C", fMAX31855_thermocouple_temp);
		}
	}
}

/*--------------------------------------------------------------------------------*/

static void StartSystem(System* pSystem)
{
	XIic_Config *pI2cConfig;
	XGpioPs_Config* pGpioConfig;

    /*-----------------------------------------------------------------*/

    pSystem->rc = XST_SUCCESS;
    pSystem->pcErr = "Success";
    pSystem->xMQTTHandle = NULL;
    pSystem->eTopic = TOPIC_SYSTEM_STATUS;

    /*-----------------------------------------------------------------*/

    InitTopicInfo();

    /*-----------------------------------------------------------------*/

	pGpioConfig = XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
	configASSERT(pGpioConfig != NULL);

	MAY_DIE({
		pSystem->rc = XGpioPs_CfgInitialize(&pSystem->gpio, pGpioConfig, pGpioConfig->BaseAddr);
		pSystem->pcErr = "XGpioPs_CfgInitialize() -> %08x";
	});
	XGpioPs_SetDirectionPin(&pSystem->gpio, LED_PIN, 1);
	XGpioPs_SetOutputEnablePin(&pSystem->gpio, LED_PIN, 1);

	BlinkLed(pSystem, 5, pdFALSE);

    /*-----------------------------------------------------------------*/

	/* Attempt to create a semaphore. */
	xIicSemaphore = xSemaphoreCreateBinary();

	if( xIicSemaphore == NULL )
	{
		goto L_DIE;
	}

	xSemaphoreTake(xIicSemaphore, portMAX_DELAY);

	pI2cConfig = XIic_LookupConfig(XPAR_IIC_0_DEVICE_ID);
	configASSERT(pI2cConfig != NULL);

	MAY_DIE({
		pSystem->rc = XIic_CfgInitialize(&pSystem->iic, pI2cConfig,	pI2cConfig->BaseAddress);
		pSystem->pcErr = "XIic_CfgInitialize() -> %08x";
	});
	XIic_IntrGlobalDisable(pI2cConfig->BaseAddress);

	MAY_DIE({
		pSystem->rc = XIic_Start(&pSystem->iic);
		pSystem->pcErr = "XIic_Start() -> %08x";
	});

	xSemaphoreGive(xIicSemaphore);

    /*-----------------------------------------------------------------*/

	/* Create the MQTT client object and connect it to the MQTT broker. */
	MAY_DIE({
		prvCreateClientAndConnectToBroker(pSystem);
		if(XST_SUCCESS == pSystem->rc) {
			BlinkLed(pSystem, 5, pdTRUE);
		}
	});

	/*-----------------------------------------------------------------*/

	MAY_DIE({
		StartBarometer(pSystem);
		pSystem->pcErr = "StartBarometer() -> %08x";
	});

	MAY_DIE({
		StartPLTempSensor(pSystem);
		pSystem->pcErr = "StartPLTempSensor() -> %08x";
	});

	MAY_DIE({
		StartHygrometer(pSystem);
		pSystem->pcErr = "StartHygroMeter() -> %08x";
	});

    /*-----------------------------------------------------------------*/

	prvPublishTopic(pSystem, TOPIC_SYSTEM_STATUS, "System started");

	return;

	/*-----------------------------------------------------------------*/

L_DIE:
	StopSystem(pSystem);
}

static void StopSystem(System* pSystem)
{
    pSystem->eTopic = TOPIC_SYSTEM_STATUS;
	if(NULL != pSystem->xMQTTHandle) {
		/* Disconnect the client. */
		( void ) MQTT_AGENT_Disconnect( pSystem->xMQTTHandle, democonfigMQTT_TIMEOUT );
	}

	StopHygrometer(pSystem);
	StopPLTempSensor(pSystem);
	StopBarometer(pSystem);

	if(pSystem->iic.IsReady) {
		xSemaphoreTake(xIicSemaphore, portMAX_DELAY);
		XIic_Stop(&pSystem->iic);
		xSemaphoreGive(xIicSemaphore);
	}

	BlinkLed(pSystem, 5, pdFALSE);

	/* End the demo by deleting all created resources. */
	configPRINTF( ( "MQTT barometer demo finished.\r\n" ) );
	vTaskDelete( NULL ); /* Delete this task. */
}

/*--------------------------------------------------------------------------------*/

static void prvMQTTConnectAndPublishTask( void * pvParameters )
{
	System tSystem;
	TickType_t xPreviousWakeTime;
    const TickType_t xSamplingPeriod = MS_TO_TICKS( SAMPLING_PERIOD_MS );

	/* Avoid compiler warnings about unused parameters. */
    ( void ) pvParameters;

    StartSystem(&tSystem);

	/* MQTT client is now connected to a broker.  Publish or perish! */
    /* Initialise the xLastWakeTime variable with the current time. */
    xPreviousWakeTime = xTaskGetTickCount();

    /*
     * Ignore errors in loop and continue forever
     */
	for(;;) {
		// Line up with next period boundary
		vTaskDelayUntil( &xPreviousWakeTime, xSamplingPeriod );

		// Publish all sensors
		SampleBarometer(&tSystem);
		SamplePLTempSensor(&tSystem);
		SampleHygrometer(&tSystem);
	}

	/* Not reached */
	StopSystem(&tSystem);
}

/*-----------------------------------------------------------*/

void vStartMQTTUZedIotDemo( void )
{
    configPRINTF( ( "Creating MQTT UZed Task...\r\n" ) );

    /* Create the task that publishes messages to the MQTT broker every five
     * seconds.  This task, in turn, creates the task that echoes data received
     * from the broker back to the broker. */
    ( void ) xTaskCreate( prvMQTTConnectAndPublishTask,        		/* The function that implements the demo task. */
                          "MQTTUZedIot",                       		/* The name to assign to the task being created. */
						  democonfigMQTT_UZED_IOT_TASK_STACK_SIZE, 	/* The size, in WORDS (not bytes), of the stack to allocate for the task being created. */
                          NULL,                                		/* The task parameter is not being used. */
                          democonfigMQTT_UZED_IOT_TASK_PRIORITY,   /* The priority at which the task being created will run. */
                          NULL                              		/* Not storing the task's handle. */
    					);
}

/*-----------------------------------------------------------*/
