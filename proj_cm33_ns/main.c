/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for non-secure
*                    application in the CM33 CPU
*
* Related Document : See README.md
*
********************************************************************************
* Copyright 2023-2025, Cypress Semiconductor Corporation (an Infineon company)
* SPDX-License-Identifier: Apache-2.0
* 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* 
*     http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/*******************************************************************************
 * Header Files
 *******************************************************************************/

#include "cybsp.h"
#include "cy_pdl.h"
#include "cycfg_pins.h"
#include "mtb_hal.h"
#include "retarget_io_init.h"
#include "cy_dfu.h"
#include "mtb_hal_spi.h"
#include "cy_scb_spi.h"
#include "cy_sysint.h"
#include "cy_dfu_logging.h"
#include "transport_spi.h"

/*******************************************************************************
 * Macros
 *******************************************************************************/

/* User defined DFU commands*/
#define CY_DFU_CMD_TOGGLE_GPIO (0xFCU) /**< DFU command: Toggle GPIO */
#define CY_DFU_CMD_READ_DATA (0xFFU)   /**< DFU command: Read Data */

/* Toggle GPIO DFU Command packet data offsets*/
#define CY_DFU_CMD_TOGGLE_GPIO_PORT_OFFSET (0U)
#define CY_DFU_CMD_TOGGLE_GPIO_PIN_OFFSET (1U)

/* Read Data DFU Command packet data offsets*/
#define CY_DFU_CMD_READ_DATA_ADDR_OFFSET (0U)
#define CY_DFU_CMD_READ_DATA_LEN_OFFSET (4U)

/* DFU command response data index */
#define CY_DFU_PACKET_DATA_IDX (4U)

/* Macro Function to get base address of GPIO port*/
#define GPIO_PORT_NUM(x) ((GPIO_PRT_Type *)&GPIO->PRT[x])

/* Device GPIO Port and Pin Macro for Toggle GPIO command*/
#define GPIO_PORT_MAX (22)
#define GPIO_PORT_MIN (0)
#define GPIO_PIN_MAX (8)
#define GPIO_PIN_MIN (0)

/* Device memories address offset for Read Data command */
#define RRAM_START_ADDRESS (RRAM_NS_SAHB_START + CYMEM_CM33_0_user_nvm_OFFSET)
#define RRAM_END_ADDRESS (RRAM_NS_SAHB_START + CYMEM_CM33_0_user_nvm_OFFSET \
                         + CYMEM_CM33_0_user_nvm_SIZE)
#define SRAM_START_ADDRESS (SRAM0_NS_SAHB_START + CYMEM_CM33_0_m33_code_OFFSET)
#define SRAM_END_ADDRESS (SRAM0_NS_SAHB_START + CYMEM_CM33_0_m55_allocatable_shared_OFFSET \
                         + CYMEM_CM33_0_m55_allocatable_shared_SIZE)
#define SOCMEM_START_ADDRESS (SOCMEM_NS_RAM_SAHB_START)
#define SOCMEM_END_ADDRESS (SOCMEM_NS_RAM_SAHB_START + SOCMEM_RAM_SIZE)
#define FLASH_START_ADDRESS (FLASH_NS_SAHB_START + CYMEM_CM33_0_m33_nvm_OFFSET)
#define FLASH_END_ADDRESS (FLASH_NS_SAHB_START + CYMEM_CM33_0_m55_trailer_OFFSET \
                              + CYMEM_CM33_0_m55_trailer_SIZE)

/* App boot address for CM55 project */
#define CM55_APP_BOOT_ADDR (CYMEM_CM33_0_m55_nvm_START + CYBSP_MCUBOOT_HEADER_SIZE)

/* The timeout value in microsecond used to wait for core to be booted */
#define CM55_BOOT_WAIT_TIME_USEC (10U)

/* Timeout for Cy_DFU_Continue(), in milliseconds */
#define DFU_SESSION_TIMEOUT_MS (20u)

/* DFU idle timeout: 300 seconds */
#define DFU_IDLE_TIMEOUT_MS (300000u)

/* DFU command timeout: 5 seconds */
#define DFU_COMMAND_TIMEOUT_MS (5000u)

/* DFU LED Macro */
#define LED_TOGGLE_INTERVAL_MS (1000u)

/********************************************************************************
 * Global Variables
 *******************************************************************************/

/*GPIO Pin config structure */
static const cy_stc_gpio_pin_config_t pin_config =
{
    .outVal = 0,
    .driveMode = CY_GPIO_DM_STRONG_IN_OFF,
    .hsiom = HSIOM_SEL_GPIO,
    .intEdge = CY_GPIO_INTR_DISABLE,
    .vtrip = CY_GPIO_VTRIP_CMOS,
    .slewRate = CY_GPIO_SLEW_FAST,
    .driveSel = CY_GPIO_DRIVE_1_2,
    .pullUpRes = CY_GPIO_PULLUP_RES_DISABLE,
    .nonSec = 1,
};

/* SPI transport HAL object  */
static mtb_hal_spi_t dfuSpiHalObj;
static cy_stc_scb_spi_context_t dfuSpiContext;

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/

static char *dfu_status_in_str(cy_en_dfu_status_t dfu_status);

cy_en_dfu_status_t dfu_custom_command_handler(uint32_t command, uint8_t *packetData,
                                              uint32_t dataSize, uint32_t *rspSize,
                                              struct cy_stc_dfu_params_s *params,
                                              bool *noResponse);
cy_en_dfu_status_t command_toggle_gpio(uint8_t *packetData, uint32_t dataSize,
                                       uint32_t *rspSize, cy_stc_dfu_params_t *params);
cy_en_dfu_status_t command_read_data(uint8_t *packetData, uint32_t dataSize,
                                     uint32_t *rspSize, cy_stc_dfu_params_t *params);

void dfu_spi_transport_init();
void dfuSpiIsr(void);
void dfuSpiTransportCallback(cy_en_dfu_transport_spi_action_t action);


/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  This is the main function. it initialize DFU to receive user-defined custom
 *  commands over SPI interface and process them. It also blinks an LED for every
 *  50 DFU Loop(~ 1 second interval) count.
 *
 * Parameters:
 *  none
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    uint32_t count = 0;
    cy_rslt_t result;
    cy_en_dfu_status_t dfu_status = CY_DFU_ERROR_UNKNOWN;
    uint32_t dfu_state = CY_DFU_STATE_NONE;

    /* Buffer to store DFU commands. */
    CY_ALIGN(4)
    static uint8_t dfu_buffer[CY_DFU_SIZEOF_DATA_BUFFER];
    /* Buffer for DFU data packets for transport API. */
    CY_ALIGN(4)
    static uint8_t dfu_packet[CY_DFU_SIZEOF_CMD_BUFFER];

    /* DFU params, used to configure DFU. */
    cy_stc_dfu_params_t dfu_params =
    {
        .timeout = DFU_SESSION_TIMEOUT_MS,
        .dataBuffer = &dfu_buffer[0],
        .packetBuffer = &dfu_packet[0],
    };

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize retarget-io middleware */
    init_retarget_io();

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen. */
    printf("\x1b[2J\x1b[;H");

    printf("****** PSOC Edge MCU: DFU User-defined Commands Demonstration ******\r\n\n");

    printf("For more projects, visit our code examples repositories:\r\n\n");

    printf("https://github.com/Infineon/Code-Examples-for-ModusToolbox-Software\r\n\n");

    /* Enable CM55 */
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);

    /* Initialize DFU Structure. */
    dfu_status = Cy_DFU_Init(&dfu_state, &dfu_params);
    if (CY_DFU_SUCCESS != dfu_status)
    {
        printf("DFU initialization failed \r\n");
        CY_ASSERT(0);
    }

    /* Register custom command handler to process user-defined commands */
    Cy_DFU_RegisterUserCommand(&dfu_params, dfu_custom_command_handler);

    /* Initialize DFU communication. This CE uses SPI interface */
    dfu_spi_transport_init();
    Cy_DFU_TransportStart(CY_DFU_SPI);
    printf("\n====================================================================\n");
    printf("\r STARTING DFU \r");
    printf("\n====================================================================\n");

    for (;;)
    {
        dfu_status = Cy_DFU_Continue(&dfu_state, &dfu_params);
        count++;
        if (CY_DFU_STATE_FINISHED == dfu_state)
        {
            printf("\n====================================================================\n");
            printf("\r DFU_STATE_FINISHED - %s \r", dfu_status_in_str(dfu_status));
            printf("\n====================================================================\n");

            count = 0u;
            Cy_DFU_Init(&dfu_state, &dfu_params);
        }
        else if (CY_DFU_STATE_FAILED == dfu_state)
        {
            printf("\n====================================================================\n");
            printf("\r DFU_STATE_FAILED - %s \r", dfu_status_in_str(dfu_status));
            printf("\n====================================================================\n");

            /* An error occurred. Handle it here.
             * This code just restarts the DFU */
            count = 0u;
            Cy_DFU_Init(&dfu_state, &dfu_params);
            Cy_DFU_TransportReset();
        }
        else if (dfu_state == CY_DFU_STATE_UPDATING)
        {
            if (dfu_status == CY_DFU_SUCCESS)
            {
                count = 0u;
            }
            else if (dfu_status == CY_DFU_ERROR_TIMEOUT)
            {
                if (count >= (DFU_COMMAND_TIMEOUT_MS / DFU_SESSION_TIMEOUT_MS))
                {
                    /* No command has been received since last 5 seconds. Restart DFU */
                    count = 0u;
                    Cy_DFU_Init(&dfu_state, &dfu_params);
                    Cy_DFU_TransportReset();
                }
            }
            else
            {
                /* Delay because Transport still may be sending error response to a host. */
                Cy_SysLib_Delay(DFU_SESSION_TIMEOUT_MS);

                /* Restart DFU. */
                count = 0u;
                Cy_DFU_TransportReset();
            }
        }
        else
        {
            /* dfu_state == CY_DFU_STATE_NONE */
            if (count >= (DFU_IDLE_TIMEOUT_MS / DFU_SESSION_TIMEOUT_MS))
            {
                /* No DFU request received in 300 seconds, lets start over.
                 * Final application can change it to either assert, reboot,
                 * enter low power mode etc, based on usecase requirements. */
                count = 0;
            }
        }

        /* Blink once per second */
        if ((count % (LED_TOGGLE_INTERVAL_MS / DFU_SESSION_TIMEOUT_MS)) == 0u)
        {
            /* Invert the USER LED1 state */
            Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);
        }
        Cy_SysLib_Delay(1);
    }
}

/*******************************************************************************
 * Function Name: dfu_status_in_str
 ********************************************************************************
 * Summary:
 *  This is the function to convert DFU status in elaborative text
 *
 * Parameters:
 *  dfu_status
 *
 * Return:
 *  string pointer
 *
 *******************************************************************************/
static char *dfu_status_in_str(cy_en_dfu_status_t dfu_status)
{
    switch (dfu_status)
    {
        case CY_DFU_SUCCESS:
            return "DFU:success";

        case CY_DFU_ERROR_VERIFY:
            return "DFU:Verification failed";

        case CY_DFU_ERROR_LENGTH:
            return "DFU:The length the packet is outside of the expected range";

        case CY_DFU_ERROR_DATA:
            return "DFU:The data in the received packet is invalid";

        case CY_DFU_ERROR_CMD:
            return "DFU:The command is not recognized";

        case CY_DFU_ERROR_CHECKSUM:
            return "DFU:The checksum does not match the expected value ";

        case CY_DFU_ERROR_ADDRESS:
            return "DFU:The wrong address";

        case CY_DFU_ERROR_TIMEOUT:
            return "DFU:The command timed out";

        case CY_DFU_ERROR_BAD_PARAM:
            return "DFU:One or more of input parameters are invalid";

        case CY_DFU_ERROR_UNKNOWN:
            return "DFU:did not recognize error";

        default:
            return "Not recognized DFU status code";
    }
}

/*******************************************************************************
 * Function Name: dfu_custom_command_handler
 ********************************************************************************
 * Summary:
 *  This is the custom command handler which is registered for processing the
 *  user-defined commands.
 *
 * Parameters:
 *  command     command ID received from DFU Host tool for handling
 *  packetData  data packet received from DFU Host Tool for processing
 *  dataSize    size of data packet
 *  rspSize     response size for response packet
 *  params      dfu params config structure
 *  noResponse  flag for response required or not
 *
 * Return:
 *  cy_en_dfu_status_t  DFU status
 *
 *******************************************************************************/
cy_en_dfu_status_t dfu_custom_command_handler(uint32_t command, uint8_t *packetData,
                                              uint32_t dataSize, uint32_t *rspSize,
                                              struct cy_stc_dfu_params_s *params,
                                              bool *noResponse)
{
    cy_en_dfu_status_t dfu_status = CY_DFU_ERROR_UNKNOWN;

    printf("\r\n DFU Command Received : 0x%0X \r\n ", (unsigned int)command);
    printf("---------------------------\r\n ");

    switch (command)
    {
        case CY_DFU_CMD_READ_DATA:
            dfu_status = command_read_data(packetData, dataSize, rspSize, params);
            break;

        case CY_DFU_CMD_TOGGLE_GPIO:
            dfu_status = command_toggle_gpio(packetData, dataSize, rspSize, params);
            break;

        default:
            dfu_status = CY_DFU_ERROR_CMD;
            *rspSize = CY_DFU_RSP_SIZE_0;
            break;
    }

    return dfu_status;
}

/*******************************************************************************
 * Function Name: command_toggle_gpio
 ********************************************************************************
 * Summary:
 *  Command Handler for user-defined command Toggle GPIO
 *
 * Parameters:
 *  packetData  data packet received from DFU Host Tool for processing
 *  dataSize    size of data packet
 *  rspSize     response size for response packet
 *  params      dfu params config structure
 *
 * Return:
 *  cy_en_dfu_status_t  DFU status
 *
 *******************************************************************************/
cy_en_dfu_status_t command_toggle_gpio(uint8_t *packetData, uint32_t dataSize,
                                       uint32_t *rspSize, cy_stc_dfu_params_t *params)
{
    cy_en_dfu_status_t status = CY_DFU_SUCCESS;
    cy_en_gpio_status_t pinstatus = CY_GPIO_SUCCESS;

    /* Extrace Port and Pin number */
    uint8_t port = packetData[CY_DFU_CMD_TOGGLE_GPIO_PORT_OFFSET];
    uint8_t pin = packetData[CY_DFU_CMD_TOGGLE_GPIO_PIN_OFFSET];

    if ((port < GPIO_PORT_MAX) && (pin < GPIO_PIN_MAX))
    {
        /* Modify the Pin configuration if required */
        if (Cy_GPIO_GetDrivemode((GPIO_PRT_Type *)GPIO_PORT_NUM(port), (uint32_t)pin) != CY_GPIO_DM_STRONG_IN_OFF)
        {
            pinstatus = Cy_GPIO_Pin_Init((GPIO_PRT_Type *)GPIO_PORT_NUM(port), (uint32_t)pin, &pin_config);
        }

        if (CY_GPIO_SUCCESS == pinstatus)
        {
            printf("Toggling GPIO Port %d Pin %d \r\n\n", port, pin);
            Cy_GPIO_Inv(GPIO_PORT_NUM(port), pin);
        }
        else
        {
            status = CY_DFU_ERROR_BAD_PARAM;
        }
    }
    else
    {
        status = CY_DFU_ERROR_BAD_PARAM;
    }

    return status;
}

/*******************************************************************************
 * Function Name: command_read_data
 ********************************************************************************
 * Summary:
 *  Command Handler for user-defined command Read Data
 *
 * Parameters:
 *  packetData  data packet received from DFU Host Tool for processing
 *  dataSize    size of data packet
 *  rspSize     response size for response packet
 *  params      dfu params config structure
 *
 * Return:
 *  cy_en_dfu_status_t  DFU status
 *
 *******************************************************************************/
cy_en_dfu_status_t command_read_data(uint8_t *packetData, uint32_t dataSize,
                                     uint32_t *rspSize, cy_stc_dfu_params_t *params)
{
    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    /* Extract Address and Length fields */
    uint8_t len = packetData[CY_DFU_CMD_READ_DATA_LEN_OFFSET];
    uint32_t addr = *((uint32_t *)(&packetData[CY_DFU_CMD_READ_DATA_ADDR_OFFSET]));

    /* Convert the address to little-endian format */
    addr = ((addr >> 24) & 0x000000FF) |
           ((addr << 24) & 0xFF000000) |
           ((addr >> 8) & 0x0000FF00) |
           ((addr << 8) & 0x00FF0000);

    if (((SRAM_START_ADDRESS <= addr) && ((addr + len) <= SRAM_END_ADDRESS)) ||
        ((RRAM_START_ADDRESS <= addr) && ((addr + len) <= RRAM_END_ADDRESS)) ||
        ((SOCMEM_START_ADDRESS <= addr) && ((addr + len) <= SOCMEM_END_ADDRESS)) ||
        ((FLASH_START_ADDRESS <= addr) && ((addr + len) <= FLASH_END_ADDRESS)))
    {
        printf("Reading %d bytes from address %p \r\n\n", len, (void *)addr);

        memcpy((void *)(params->packetBuffer + CY_DFU_PACKET_DATA_IDX), (void *)addr, len);

        *rspSize = len;
    }
    else
    {
        printf("ERROR: Cannot read %d bytes from address %p. "
               "Valid Address range is listed below.\r\n\n", len, (void *)addr);
        printf("  RRAM   : %p - %p\r\n", (void *)RRAM_START_ADDRESS, (void *)(RRAM_END_ADDRESS - 1));
        printf("  SRAM   : %p - %p\r\n", (void *)SRAM_START_ADDRESS, (void *)(SRAM_END_ADDRESS - 1));
        printf("  SOCMEM : %p - %p\r\n", (void *)SOCMEM_START_ADDRESS, (void *)(SOCMEM_END_ADDRESS - 1));
        printf("  FLASH  : %p - %p\r\n\n", (void *)FLASH_START_ADDRESS, (void *)(FLASH_END_ADDRESS - 1));

        rspSize = CY_DFU_RSP_SIZE_0;
        status = CY_DFU_ERROR_BAD_PARAM;
    }

    return status;
}

/*******************************************************************************
 * Function Name: dfu_spi_transport_init
 ********************************************************************************
 * Summary:
 *  Configure DFU I2C transport to receive data from DFU Host Tool
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void dfu_spi_transport_init()
{
    cy_en_scb_spi_status_t pdlSpiStatus;
    cy_en_sysint_status_t pdlSysIntStatus;
    cy_rslt_t halStatus;

    pdlSpiStatus = Cy_SCB_SPI_Init(DFU_SPI_HW, &DFU_SPI_config, &dfuSpiContext);
    if (CY_SCB_SPI_SUCCESS != pdlSpiStatus)
    {
        CY_DFU_LOG_ERR("Error during SPI PDL initialization. Status: %X", pdlSpiStatus);
    }
    else
    {
        halStatus = mtb_hal_spi_setup(&dfuSpiHalObj, &DFU_SPI_hal_config, &dfuSpiContext, NULL);
        if (CY_RSLT_SUCCESS != halStatus)
        {
            CY_DFU_LOG_ERR("Error during SPI HAL initialization. Status: %lX", halStatus);
        }
        else
        {
            /* Workaround. See BSP-6784 */
            dfuSpiHalObj.is_target = true;
            cy_stc_sysint_t spiIsrCfg =
            {
                .intrSrc = DFU_SPI_IRQ,
                .intrPriority = 3U
            };

            pdlSysIntStatus = Cy_SysInt_Init(&spiIsrCfg, dfuSpiIsr);
            if (CY_SYSINT_SUCCESS != pdlSysIntStatus)
            {
                CY_DFU_LOG_ERR("Error during SPI Interrupt initialization. Status: %X", pdlSysIntStatus);
            }
            else
            {
                NVIC_EnableIRQ((IRQn_Type)spiIsrCfg.intrSrc);
                CY_DFU_LOG_INF("SPI transport is initialized");
            }
        }
    }

    cy_stc_dfu_transport_spi_cfg_t spiTransportCfg =
    {
        .spi = &dfuSpiHalObj,
        .callback = dfuSpiTransportCallback,
    };

    Cy_DFU_TransportSpiConfig(&spiTransportCfg);
}

/*******************************************************************************
 * Function Name: dfuSpiIsr
 ********************************************************************************
 * Summary:
 *  SPI interrupt callback
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void dfuSpiIsr(void)
{
    mtb_hal_spi_process_interrupt(&dfuSpiHalObj);
}

/*******************************************************************************
 * Function Name: dfuSpiTransportCallback
 ********************************************************************************
 * Summary:
 *  Callback to enable or disable transport
 *
 * Parameters:
 *  action  Callback trigger
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void dfuSpiTransportCallback(cy_en_dfu_transport_spi_action_t action)
{
    if (action == CY_DFU_TRANSPORT_SPI_INIT)
    {
        Cy_SCB_SPI_Enable(DFU_SPI_HW);
        CY_DFU_LOG_INF("SPI transport is enabled");
    }
    else if (action == CY_DFU_TRANSPORT_SPI_DEINIT)
    {
        Cy_SCB_SPI_Disable(DFU_SPI_HW, &dfuSpiContext);
        CY_DFU_LOG_INF("SPI transport is disabled");
    }
}

/* [] END OF FILE */
