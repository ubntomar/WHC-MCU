#include "ch32v30x_it.h"

void NMI_Handler(void) __attribute__((interrupt("machine")));
void HardFault_Handler(void) __attribute__((interrupt("machine")));

void NMI_Handler(void)
{
    while (1) {}
}

void HardFault_Handler(void)
{
    NVIC_SystemReset();
    while (1) {}
}
