/*
 * USB CDC Echo — CH32V305FBP6 (CH32V305F-R0-1v0)
 *
 * La placa aparece como puerto serie virtual (USB CDC ACM) en el PC.
 * Todo lo que envies por el terminal se devuelve como eco.
 *
 * Conectar USB-C (P7) al PC. NO usar USB-A (P6) al mismo tiempo.
 * No necesita jumpers de LED — usa solo el USB-C.
 */

#include "UART.h"
#include "debug.h"

int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();

    RCC_Configuration();

    /* Timer 2 para timeouts del CDC */
    TIM2_Init();

    /* Init UART2 interno (usado como buffer por el CDC stack) */
    UART2_Init(1, DEF_UARTx_BAUDRATE, DEF_UARTx_STOPBIT, DEF_UARTx_PARITY);

    /* Init USB High-Speed device como CDC */
    USBHS_RCC_Init();
    USBHS_Device_Init(ENABLE);

    while (1) {
        UART2_DataRx_Deal();
        UART2_DataTx_Deal();
    }
}
