/*
 * USB CDC Echo directo — CH32V305FBP6 (CH32V305F-R0-1v0)
 *
 * Todo lo que se recibe por el puerto serie virtual USB se devuelve
 * inmediatamente como eco. No usa UART — el eco es 100% dentro del USB.
 *
 * Conectar USB-C (P7) al PC. Aparece como /dev/ttyACMx.
 * Probar con:  echo "hola" > /dev/ttyACMx
 *              cat /dev/ttyACMx   (en otra terminal)
 */

#include "ch32v30x_usbhs_device.h"
#include "debug.h"

int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();

    /* Init USB High-Speed device como CDC */
    USBHS_RCC_Init();
    USBHS_Device_Init(ENABLE);

    while (1) {
        /* Eco directo: si llego data por USB, lo reenvía */
        USBHS_CDC_Echo();
    }
}
