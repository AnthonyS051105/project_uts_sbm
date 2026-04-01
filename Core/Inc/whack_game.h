#ifndef WHACK_GAME_H
#define WHACK_GAME_H

#include "main.h"

/* ------------------------------------------------------------------ *
 *  Whack-a-LED Game — Fase 3
 * ------------------------------------------------------------------ */

void WhackGame_Init(void);
void WhackGame_Run(void);
void WhackGame_Reset(void);

void WhackGame_BTN1_Press(void);
void WhackGame_BTN2_Press(void);
void WhackGame_TIM3_ISR(void); // Call this from TIM3_IRQHandler

#endif /* WHACK_GAME_H */
