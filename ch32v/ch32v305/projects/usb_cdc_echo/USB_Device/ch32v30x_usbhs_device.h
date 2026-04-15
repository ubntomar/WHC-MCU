#ifndef __CH32V30X_USBHS_DEVICE_H__
#define __CH32V30X_USBHS_DEVICE_H__

#include "debug.h"
#include "string.h"
#include "ch32v30x_usb.h"
#include "usb_desc.h"
#include "ch32v30x_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Macros */
#define pUSBHS_SetupReqPak            ((PUSB_SETUP_REQ)USBHS_EP0_Buf)

#define DEF_UEP_IN                    0x80
#define DEF_UEP_OUT                   0x00
#define DEF_UEP_BUSY                  0x01
#define DEF_UEP_FREE                  0x00
#define DEF_UEP_NUM                   16
#define DEF_UEP0                      0x00
#define DEF_UEP1                      0x01
#define DEF_UEP2                      0x02
#define DEF_UEP3                      0x03

#define USBHSD_UEP_CFG_BASE           0x40023410
#define USBHSD_UEP_BUF_MOD_BASE       0x40023418
#define USBHSD_UEP_RXDMA_BASE         0x40023420
#define USBHSD_UEP_TXDMA_BASE         0x4002345C
#define USBHSD_UEP_TXLEN_BASE         0x400234DC
#define USBHSD_UEP_TXCTL_BASE         0x400234DE
#define USBHSD_UEP_TX_EN( N )         ( (uint16_t)( 0x01 << N ) )
#define USBHSD_UEP_RX_EN( N )         ( (uint16_t)( 0x01 << ( N + 16 ) ) )
#define USBHSD_UEP_DOUBLE_BUF( N )    ( (uint16_t)( 0x01 << N ) )
#define DEF_UEP_DMA_LOAD              0
#define DEF_UEP_CPY_LOAD              1
#define USBHSD_UEP_RXDMA( N )         ( *((volatile uint32_t *)( USBHSD_UEP_RXDMA_BASE + ( N - 1 ) * 0x04 ) ) )
#define USBHSD_UEP_RXBUF( N )         ( (uint8_t *)(*((volatile uint32_t *)( USBHSD_UEP_RXDMA_BASE + ( N - 1 ) * 0x04 ) ) ) + 0x20000000 )
#define USBHSD_UEP_TXCTRL( N )        ( *((volatile uint8_t *)( USBHSD_UEP_TXCTL_BASE + ( N - 1 ) * 0x04 ) ) )
#define USBHSD_UEP_TXDMA( N )         ( *((volatile uint32_t *)( USBHSD_UEP_TXDMA_BASE + ( N - 1 ) * 0x04 ) ) )
#define USBHSD_UEP_TXBUF( N )         ( (uint8_t *)(*((volatile uint32_t *)( USBHSD_UEP_TXDMA_BASE + ( N - 1 ) * 0x04 ) ) ) + 0x20000000 )
#define USBHSD_UEP_TLEN( N )          ( *((volatile uint16_t *)( USBHSD_UEP_TXLEN_BASE + ( N - 1 ) * 0x04 ) ) )

/* USB speed */
#define USBHS_SPEED_TYPE_MASK         ((uint8_t)(0x03))
#define USBHS_SPEED_LOW               ((uint8_t)(0x02))
#define USBHS_SPEED_FULL              ((uint8_t)(0x00))
#define USBHS_SPEED_HIGH              ((uint8_t)(0x01))

/* CDC class requests */
#define CDC_GET_LINE_CODING           0x21
#define CDC_SET_LINE_CODING           0x20
#define CDC_SET_LINE_CTLSTE           0x22
#define CDC_SEND_BREAK                0x23

/******************************************************************************/
/* Variables */
extern const uint8_t *pUSBHS_Descr;

extern volatile uint8_t  USBHS_SetupReqCode;
extern volatile uint8_t  USBHS_SetupReqType;
extern volatile uint16_t USBHS_SetupReqValue;
extern volatile uint16_t USBHS_SetupReqIndex;
extern volatile uint16_t USBHS_SetupReqLen;

extern volatile uint8_t  USBHS_DevConfig;
extern volatile uint8_t  USBHS_DevAddr;
extern volatile uint8_t  USBHS_DevSleepStatus;
extern volatile uint8_t  USBHS_DevEnumStatus;
extern volatile uint16_t USBHS_DevMaxPackLen;
extern volatile uint8_t  USBHS_DevSpeed;

extern volatile uint8_t  USBHS_Endp_Busy[];

extern __attribute__((aligned(4))) uint8_t USBHS_EP0_Buf[];
extern __attribute__((aligned(4))) uint8_t USBHS_EP2_Tx_Buf[];
extern __attribute__((aligned(4))) uint8_t USBHS_EP2_Rx_Buf[];

/******************************************************************************/
/* Functions */
extern void    USBHS_RCC_Init(void);
extern void    USBHS_Device_Endp_Init(void);
extern void    USBHS_Device_Init(FunctionalState sta);
extern void    USBHS_CDC_Echo(void);
extern void    USBHS_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif
