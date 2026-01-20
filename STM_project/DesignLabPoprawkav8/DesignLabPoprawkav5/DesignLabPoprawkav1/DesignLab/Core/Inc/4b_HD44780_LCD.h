#ifndef __4b_HD44780_LCD_H
#define __4b_HD44780_LCD_H

#include "stm32f3xx_hal.h"

void LCD_Confirm(void);
void LCD_HD44780_Init(void);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Print(char *message);
void LCD_SendCMD(uint8_t cmd);
void LCD_SendData(uint8_t data);
void LCD_SendNibble(uint8_t nibble);

#endif
