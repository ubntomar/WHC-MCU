/*
 * USBHS CDC Echo — eco directo sin UART
 *
 * Flujo:
 *   1. Host envía datos → EP2 OUT → Echo_Rx_Buf (via DMA)
 *   2. Main loop copia Echo_Rx_Buf → Echo_Tx_Buf, arma EP2 IN
 *   3. Host lee datos ← EP2 IN ← Echo_Tx_Buf
 *   4. Tras enviar, se rearma EP2 OUT para recibir más
 */

#include "ch32v30x_usbhs_device.h"

/******************************************************************************/
/* Variables globales del USB */

/* Descriptor pointer */
const uint8_t    *pUSBHS_Descr;

/* Setup Request */
volatile uint8_t  USBHS_SetupReqCode;
volatile uint8_t  USBHS_SetupReqType;
volatile uint16_t USBHS_SetupReqValue;
volatile uint16_t USBHS_SetupReqIndex;
volatile uint16_t USBHS_SetupReqLen;

/* Device Status */
volatile uint8_t  USBHS_DevConfig;
volatile uint8_t  USBHS_DevAddr;
volatile uint16_t USBHS_DevMaxPackLen;
volatile uint8_t  USBHS_DevSpeed;
volatile uint8_t  USBHS_DevSleepStatus;
volatile uint8_t  USBHS_DevEnumStatus;

/* Endpoint buffers */
__attribute__((aligned(4))) uint8_t USBHS_EP0_Buf[DEF_USBD_UEP0_SIZE];
__attribute__((aligned(4))) uint8_t USBHS_EP2_Tx_Buf[DEF_USB_EP2_HS_SIZE];
__attribute__((aligned(4))) uint8_t USBHS_EP2_Rx_Buf[DEF_USB_EP2_HS_SIZE];
__attribute__((aligned(4))) uint8_t USBHS_EP3_Tx_Buf[DEF_USB_EP3_HS_SIZE];

/* Endpoint busy flags */
volatile uint8_t USBHS_Endp_Busy[DEF_UEP_NUM];

/* CDC line coding (7 bytes: baudrate[4], stopbits, parity, databits) */
static uint8_t CDC_LineCoding[8] = {
    0x00, 0xC2, 0x01, 0x00,   /* 115200 baud (little-endian) */
    0x00,                       /* 1 stop bit */
    0x00,                       /* no parity */
    0x08,                       /* 8 data bits */
    0x00                        /* reserved */
};

/* Echo state */
static volatile uint16_t Echo_Rx_Len   = 0;
static volatile uint8_t  Echo_Rx_Ready = 0;
static volatile uint8_t  Echo_Tx_Busy  = 0;

/* Other-speed config descriptors (definidos en usb_desc.c) */

/******************************************************************************/
/* ISR declaration — "machine" genera mret (GCC 15 compatible) */
void USBHS_IRQHandler(void) __attribute__((interrupt("machine")));

/*********************************************************************
 * @fn      USBHS_RCC_Init
 * @brief   Configura clocks del USBHS PHY.
 */
void USBHS_RCC_Init(void)
{
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
}

/*********************************************************************
 * @fn      USBHS_Device_Endp_Init
 * @brief   Inicializa endpoints.
 *          EP0: control, EP2 IN/OUT: bulk data, EP3 IN: notificación CDC
 */
void USBHS_Device_Endp_Init(void)
{
    USBHSD->ENDP_CONFIG = USBHS_UEP3_T_EN | USBHS_UEP3_R_EN |
                           USBHS_UEP2_T_EN | USBHS_UEP2_R_EN;

    USBHSD->UEP0_MAX_LEN = DEF_USBD_UEP0_SIZE;
    USBHSD->UEP2_MAX_LEN = DEF_USB_EP2_HS_SIZE;
    USBHSD->UEP3_MAX_LEN = DEF_USB_EP3_HS_SIZE;

    USBHSD->UEP0_DMA    = (uint32_t)USBHS_EP0_Buf;
    USBHSD->UEP2_RX_DMA = (uint32_t)USBHS_EP2_Rx_Buf;
    USBHSD->UEP2_TX_DMA = (uint32_t)USBHS_EP2_Tx_Buf;
    USBHSD->UEP3_RX_DMA = (uint32_t)USBHS_EP3_Tx_Buf;

    USBHSD->UEP0_TX_LEN  = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;

    USBHSD->UEP2_TX_LEN  = 0;
    USBHSD->UEP2_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP2_RX_CTRL = USBHS_UEP_R_RES_ACK;

    USBHSD->UEP3_TX_LEN  = 0;
    USBHSD->UEP3_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP3_RX_CTRL = USBHS_UEP_R_RES_ACK;

    for (uint8_t i = 0; i < DEF_UEP_NUM; i++)
        USBHS_Endp_Busy[i] = 0;
}

/*********************************************************************
 * @fn      USBHS_Device_Init
 * @brief   Inicializa dispositivo USB HS.
 */
void USBHS_Device_Init(FunctionalState sta)
{
    if (sta) {
        USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
        Delay_Us(10);
        USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
        USBHSD->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
        USBHSD->CONTROL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_HIGH;
        USBHSD->INT_EN = USBHS_UIE_SETUP_ACT | USBHS_UIE_TRANSFER |
                          USBHS_UIE_DETECT | USBHS_UIE_SUSPEND;
        USBHS_Device_Endp_Init();
        USBHSD->CONTROL |= USBHS_UC_DEV_PU_EN;
        NVIC_EnableIRQ(USBHS_IRQn);
    } else {
        USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
        Delay_Us(10);
        USBHSD->CONTROL = 0;
        NVIC_DisableIRQ(USBHS_IRQn);
    }
}

/*********************************************************************
 * @fn      USBHS_CDC_Echo
 * @brief   Llamar desde main loop. Si hay datos recibidos, los copia
 *          al buffer TX y los envía de vuelta al host.
 */
void USBHS_CDC_Echo(void)
{
    if (Echo_Rx_Ready && !Echo_Tx_Busy) {
        /* Copiar datos recibidos al buffer de transmisión */
        memcpy(USBHS_EP2_Tx_Buf, USBHS_EP2_Rx_Buf, Echo_Rx_Len);

        /* Armar EP2 IN para enviar el eco */
        NVIC_DisableIRQ(USBHS_IRQn);
        USBHSD->UEP2_TX_DMA = (uint32_t)USBHS_EP2_Tx_Buf;
        USBHSD->UEP2_TX_LEN = Echo_Rx_Len;
        USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
        Echo_Tx_Busy = 1;
        Echo_Rx_Ready = 0;
        NVIC_EnableIRQ(USBHS_IRQn);
    }
}

/*********************************************************************
 * @fn      USBHS_IRQHandler
 * @brief   Interrupción USBHS — maneja enumeración, SETUP, y datos.
 */
void USBHS_IRQHandler(void)
{
    uint8_t  intflag, intst, errflag;
    uint16_t len;

    intflag = USBHSD->INT_FG;
    intst   = USBHSD->INT_ST;

    if (intflag & USBHS_UIF_TRANSFER)
    {
        switch (intst & USBHS_UIS_TOKEN_MASK)
        {
            /* ============ IN (device → host) ============ */
            case USBHS_UIS_TOKEN_IN:
                switch (intst & (USBHS_UIS_TOKEN_MASK | USBHS_UIS_ENDP_MASK))
                {
                    /* EP0 IN — envío de descriptores, etc. */
                    case USBHS_UIS_TOKEN_IN | DEF_UEP0:
                        if (USBHS_SetupReqLen == 0)
                            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;

                        if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD) {
                            switch (USBHS_SetupReqCode) {
                                case USB_GET_DESCRIPTOR:
                                    len = (USBHS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                                    memcpy(USBHS_EP0_Buf, pUSBHS_Descr, len);
                                    USBHS_SetupReqLen -= len;
                                    pUSBHS_Descr += len;
                                    USBHSD->UEP0_TX_LEN = len;
                                    USBHSD->UEP0_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                                    break;
                                case USB_SET_ADDRESS:
                                    USBHSD->DEV_AD = USBHS_DevAddr;
                                    break;
                                default:
                                    USBHSD->UEP0_TX_LEN = 0;
                                    break;
                            }
                        }
                        break;

                    /* EP2 IN completo — eco enviado, rearmar RX */
                    case USBHS_UIS_TOKEN_IN | DEF_UEP2:
                        USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                        USBHSD->UEP2_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                        Echo_Tx_Busy = 0;
                        /* Rearmar EP2 OUT para recibir el próximo paquete */
                        USBHSD->UEP2_RX_CTRL = (USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
                        break;

                    /* EP3 IN (notificación CDC, no usado en echo) */
                    case USBHS_UIS_TOKEN_IN | DEF_UEP3:
                        USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                        USBHSD->UEP3_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                        break;

                    default:
                        break;
                }
                break;

            /* ============ OUT (host → device) ============ */
            case USBHS_UIS_TOKEN_OUT:
                switch (intst & (USBHS_UIS_TOKEN_MASK | USBHS_UIS_ENDP_MASK))
                {
                    /* EP0 OUT — data stage de SET_LINE_CODING */
                    case USBHS_UIS_TOKEN_OUT | DEF_UEP0:
                        if (intst & USBHS_UIS_TOG_OK) {
                            if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
                                if (USBHS_SetupReqCode == CDC_SET_LINE_CODING) {
                                    memcpy(CDC_LineCoding, USBHS_EP0_Buf, 7);
                                }
                                USBHS_SetupReqLen = 0;
                            }
                            if (USBHS_SetupReqLen == 0) {
                                USBHSD->UEP0_TX_LEN  = 0;
                                USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
                            }
                        }
                        break;

                    /* EP2 OUT — datos recibidos del host → marcar para eco */
                    case USBHS_UIS_TOKEN_OUT | DEF_UEP2:
                        USBHSD->UEP2_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
                        Echo_Rx_Len = USBHSD->RX_LEN;
                        Echo_Rx_Ready = 1;
                        /* NAK hasta que main loop procese y envíe el eco */
                        USBHSD->UEP2_RX_CTRL = (USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
                        break;

                    default:
                        break;
                }
                break;

            case USBHS_UIS_TOKEN_SOF:
                break;

            default:
                break;
        }
        USBHSD->INT_FG = USBHS_UIF_TRANSFER;
    }
    else if (intflag & USBHS_UIF_SETUP_ACT)
    {
        /* ============ SETUP (control requests) ============ */
        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;

        USBHS_SetupReqType  = pUSBHS_SetupReqPak->bRequestType;
        USBHS_SetupReqCode  = pUSBHS_SetupReqPak->bRequest;
        USBHS_SetupReqLen   = pUSBHS_SetupReqPak->wLength;
        USBHS_SetupReqValue = pUSBHS_SetupReqPak->wValue;
        USBHS_SetupReqIndex = pUSBHS_SetupReqPak->wIndex;

        len = 0;
        errflag = 0;

        if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
        {
            /* --- CDC class requests --- */
            if (USBHS_SetupReqType & USB_REQ_TYP_CLASS) {
                switch (USBHS_SetupReqCode) {
                    case CDC_GET_LINE_CODING:
                        pUSBHS_Descr = CDC_LineCoding;
                        len = 7;
                        break;
                    case CDC_SET_LINE_CODING:
                        break;
                    case CDC_SET_LINE_CTLSTE:
                        break;
                    case CDC_SEND_BREAK:
                        break;
                    default:
                        errflag = 0xFF;
                        break;
                }
            } else {
                errflag = 0xFF;
            }
            len = (USBHS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
            memcpy(USBHS_EP0_Buf, pUSBHS_Descr, len);
            pUSBHS_Descr += len;
        }
        else
        {
            /* --- Standard USB requests --- */
            switch (USBHS_SetupReqCode)
            {
                case USB_GET_DESCRIPTOR:
                    switch ((uint8_t)(USBHS_SetupReqValue >> 8))
                    {
                        case USB_DESCR_TYP_DEVICE:
                            pUSBHS_Descr = MyDevDescr;
                            len = DEF_USBD_DEVICE_DESC_LEN;
                            break;

                        case USB_DESCR_TYP_CONFIG:
                            if ((USBHSD->SPEED_TYPE & USBHS_SPEED_TYPE_MASK) == USBHS_SPEED_HIGH) {
                                USBHS_DevSpeed = USBHS_SPEED_HIGH;
                                USBHS_DevMaxPackLen = DEF_USBD_HS_PACK_SIZE;
                            } else {
                                USBHS_DevSpeed = USBHS_SPEED_FULL;
                                USBHS_DevMaxPackLen = DEF_USBD_FS_PACK_SIZE;
                            }
                            if (USBHS_DevSpeed == USBHS_SPEED_HIGH) {
                                pUSBHS_Descr = MyCfgDescr_HS;
                                len = DEF_USBD_CONFIG_HS_DESC_LEN;
                            } else {
                                pUSBHS_Descr = MyCfgDescr_FS;
                                len = DEF_USBD_CONFIG_FS_DESC_LEN;
                            }
                            break;

                        case USB_DESCR_TYP_STRING:
                            switch ((uint8_t)(USBHS_SetupReqValue & 0xFF)) {
                                case DEF_STRING_DESC_LANG:
                                    pUSBHS_Descr = MyLangDescr;
                                    len = DEF_USBD_LANG_DESC_LEN;
                                    break;
                                case DEF_STRING_DESC_MANU:
                                    pUSBHS_Descr = MyManuInfo;
                                    len = DEF_USBD_MANU_DESC_LEN;
                                    break;
                                case DEF_STRING_DESC_PROD:
                                    pUSBHS_Descr = MyProdInfo;
                                    len = DEF_USBD_PROD_DESC_LEN;
                                    break;
                                case DEF_STRING_DESC_SERN:
                                    pUSBHS_Descr = MySerNumInfo;
                                    len = DEF_USBD_SN_DESC_LEN;
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                            break;

                        case USB_DESCR_TYP_QUALIF:
                            pUSBHS_Descr = MyQuaDesc;
                            len = DEF_USBD_QUALFY_DESC_LEN;
                            break;

                        case USB_DESCR_TYP_BOS:
                            errflag = 0xFF;
                            break;

                        case USB_DESCR_TYP_SPEED:
                            if (USBHS_DevSpeed == USBHS_SPEED_HIGH) {
                                memcpy(&TAB_USB_HS_OSC_DESC[2], &MyCfgDescr_FS[2], DEF_USBD_CONFIG_FS_DESC_LEN - 2);
                                pUSBHS_Descr = TAB_USB_HS_OSC_DESC;
                                len = DEF_USBD_CONFIG_FS_DESC_LEN;
                            } else if (USBHS_DevSpeed == USBHS_SPEED_FULL) {
                                memcpy(&TAB_USB_FS_OSC_DESC[2], &MyCfgDescr_HS[2], DEF_USBD_CONFIG_HS_DESC_LEN - 2);
                                pUSBHS_Descr = TAB_USB_FS_OSC_DESC;
                                len = DEF_USBD_CONFIG_HS_DESC_LEN;
                            } else {
                                errflag = 0xFF;
                            }
                            break;

                        default:
                            errflag = 0xFF;
                            break;
                    }
                    if (USBHS_SetupReqLen > len) USBHS_SetupReqLen = len;
                    len = (USBHS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                    memcpy(USBHS_EP0_Buf, pUSBHS_Descr, len);
                    pUSBHS_Descr += len;
                    break;

                case USB_SET_ADDRESS:
                    USBHS_DevAddr = (uint16_t)(USBHS_SetupReqValue & 0xFF);
                    break;

                case USB_GET_CONFIGURATION:
                    USBHS_EP0_Buf[0] = USBHS_DevConfig;
                    if (USBHS_SetupReqLen > 1) USBHS_SetupReqLen = 1;
                    break;

                case USB_SET_CONFIGURATION:
                    USBHS_DevConfig = (uint8_t)(USBHS_SetupReqValue & 0xFF);
                    USBHS_DevEnumStatus = 0x01;
                    break;

                case USB_CLEAR_FEATURE:
                    if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) {
                        if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == 0x01)
                            USBHS_DevSleepStatus &= ~0x01;
                        else
                            errflag = 0xFF;
                    } else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                        if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT) {
                            switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF)) {
                                case (DEF_UEP2 | DEF_UEP_IN):
                                    USBHSD->UEP2_TX_CTRL = USBHS_UEP_T_RES_NAK;
                                    break;
                                case (DEF_UEP2 | DEF_UEP_OUT):
                                    USBHSD->UEP2_RX_CTRL = USBHS_UEP_R_RES_ACK;
                                    break;
                                case (DEF_UEP3 | DEF_UEP_IN):
                                    USBHSD->UEP3_TX_CTRL = USBHS_UEP_T_RES_NAK;
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                        } else {
                            errflag = 0xFF;
                        }
                    } else {
                        errflag = 0xFF;
                    }
                    break;

                case USB_SET_FEATURE:
                    if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) {
                        if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == USB_REQ_FEAT_REMOTE_WAKEUP) {
                            if (MyCfgDescr_FS[7] & 0x20)
                                USBHS_DevSleepStatus |= 0x01;
                            else
                                errflag = 0xFF;
                        } else {
                            errflag = 0xFF;
                        }
                    } else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                        if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT) {
                            switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF)) {
                                case (DEF_UEP2 | DEF_UEP_IN):
                                    USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL;
                                    break;
                                case (DEF_UEP2 | DEF_UEP_OUT):
                                    USBHSD->UEP2_RX_CTRL = (USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL;
                                    break;
                                case (DEF_UEP3 | DEF_UEP_IN):
                                    USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL;
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                        }
                    }
                    break;

                case USB_GET_INTERFACE:
                    USBHS_EP0_Buf[0] = 0x00;
                    if (USBHS_SetupReqLen > 1) USBHS_SetupReqLen = 1;
                    break;

                case USB_SET_INTERFACE:
                    break;

                case USB_GET_STATUS:
                    USBHS_EP0_Buf[0] = 0x00;
                    USBHS_EP0_Buf[1] = 0x00;
                    if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                        switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF)) {
                            case (DEF_UEP2 | DEF_UEP_IN):
                                if (((USBHSD->UEP2_TX_CTRL) & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL)
                                    USBHS_EP0_Buf[0] = 0x01;
                                break;
                            case (DEF_UEP2 | DEF_UEP_OUT):
                                if (((USBHSD->UEP2_RX_CTRL) & USBHS_UEP_R_RES_MASK) == USBHS_UEP_R_RES_STALL)
                                    USBHS_EP0_Buf[0] = 0x01;
                                break;
                            case (DEF_UEP3 | DEF_UEP_IN):
                                if (((USBHSD->UEP3_TX_CTRL) & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL)
                                    USBHS_EP0_Buf[0] = 0x01;
                                break;
                            default:
                                errflag = 0xFF;
                                break;
                        }
                    } else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) {
                        if (USBHS_DevSleepStatus & 0x01)
                            USBHS_EP0_Buf[0] = 0x02;
                    }
                    if (USBHS_SetupReqLen > 2) USBHS_SetupReqLen = 2;
                    break;

                default:
                    errflag = 0xFF;
                    break;
            }
        }

        if (errflag == 0xFF) {
            USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_STALL;
            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_STALL;
        } else {
            if (USBHS_SetupReqType & DEF_UEP_IN) {
                len = (USBHS_SetupReqLen > DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                USBHS_SetupReqLen -= len;
                USBHSD->UEP0_TX_LEN  = len;
                USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
            } else {
                if (USBHS_SetupReqLen == 0) {
                    USBHSD->UEP0_TX_LEN  = 0;
                    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
                } else {
                    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
                }
            }
        }
        USBHSD->INT_FG = USBHS_UIF_SETUP_ACT;
    }
    else if (intflag & USBHS_UIF_BUS_RST)
    {
        /* Bus reset — reiniciar estado */
        USBHS_DevConfig = 0;
        USBHS_DevAddr = 0;
        USBHS_DevSleepStatus = 0;
        USBHS_DevEnumStatus = 0;
        Echo_Rx_Ready = 0;
        Echo_Tx_Busy = 0;
        Echo_Rx_Len = 0;

        USBHSD->DEV_AD = 0;
        USBHS_Device_Endp_Init();
        USBHSD->INT_FG = USBHS_UIF_BUS_RST;
    }
    else if (intflag & USBHS_UIF_SUSPEND)
    {
        USBHSD->INT_FG = USBHS_UIF_SUSPEND;
        Delay_Us(10);
        if (USBHSD->MIS_ST & USBHS_UMS_SUSPEND)
            USBHS_DevSleepStatus |= 0x02;
        else
            USBHS_DevSleepStatus &= ~0x02;
    }
    else
    {
        USBHSD->INT_FG = intflag;
    }
}
