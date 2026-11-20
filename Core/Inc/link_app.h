#ifndef LINK_APP_H
#define LINK_APP_H

/* Glue between main.c and the Modbus link (UART build only): USART1 driver,
 * Modbus core, register map, link hold timer and clear policy. */

#include <stdbool.h>
#include <stdint.h>

/* Reset cause (RCC_CSR is read and cleared on the first call only), safe
 * tracker, link state, target 0, USART1. Call after the ADC DMA start.
 * The first call also re-arms the remote-clear counter and last_fault on a
 * power-on reset (or missing magic); other resets keep them. The fault loop
 * calls it again with the loaded fault code, which becomes last_fault;
 * everywhere else active_fault is ERROR_NONE. */
void LinkApp_Init(uint8_t active_fault);

/* Every loop pass, main loop and fault loop: answers at most one frame and
 * runs the link hold timer. active_fault is ERROR_NONE in the main loop and
 * the loaded code in the fault loop. After the reply to an approved remote
 * clear it does not return: it decrements the counter, clears the fault page
 * (ClearErrorCode) and resets with NVIC_SystemReset. */
void LinkApp_Service(uint8_t active_fault);

/* Commanded air target in °C, 0 = off. */
float LinkApp_AirTarget(void);

/* True while the link is in hold (silent for LINK_SILENCE_MS, not yet expired). */
bool LinkApp_InHold(void);

/* True once a valid frame has arrived, until a hold expires; the next valid
 * frame makes it true again. False means "waiting for the link". */
bool LinkApp_LinkUp(void);

/* HAL tick of the last answered frame, for the LED3 frame flash. */
uint32_t LinkApp_LastFrameTick(void);

#endif /* LINK_APP_H */
