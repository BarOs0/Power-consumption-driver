#include "4b_HD44780_LCD.h"
#include "main.h"

// impuls zatwierdzajacy posczegolna paczke danych (w tym przypadku nibble)
void LCD_Confirm(void){
	HAL_GPIO_WritePin(E_GPIO_Port, E_Pin, GPIO_PIN_SET);
	HAL_Delay(1); // musi chwilke trwac
	HAL_GPIO_WritePin(E_GPIO_Port, E_Pin, GPIO_PIN_RESET);
	HAL_Delay(1); // patrz wyzej
}

// funkcja do wysylania nibble w strone lcd - do komunikacji np komendy itd
// nibble bo bede sobie dzialal w trybie 4 bitowym (mniej laczenia)
void LCD_SendNibble(uint8_t nibble){
	HAL_GPIO_WritePin(D4_GPIO_Port, D4_Pin, (nibble >> 0) & 0x01);
	HAL_GPIO_WritePin(D5_GPIO_Port, D5_Pin, (nibble >> 1) & 0x01);
	HAL_GPIO_WritePin(D6_GPIO_Port, D6_Pin, (nibble >> 2) & 0x01);
	HAL_GPIO_WritePin(D7_GPIO_Port, D7_Pin, (nibble >> 3) & 0x01);
}

void LCD_SendCMD(uint8_t cmd){
	HAL_GPIO_WritePin(RS_GPIO_Port, RS_Pin, GPIO_PIN_RESET); // RS = 0 -> TRYB KOMENDY
	LCD_SendNibble(cmd >> 4); // starsza czesc bajtu
	LCD_Confirm();
	LCD_SendNibble(cmd & 0x0F); // mlodsza czesc
	LCD_Confirm();
	HAL_Delay(3);
}

/*ZESTAW KOMEND DLA HD44780
	0x01 – czyści ekran (Clear Display)
	0x02 – kursor na początek (Return Home)
	0x06 – automatyczne przesuwanie kursora w prawo po wpisaniu znaku (Entry Mode Set)
	0x08 – wyłącza wyświetlanie (Display OFF)
	0x0C – włącza wyświetlanie, kursor wyłączony (Display ON, Cursor OFF)
	0x0E – włącza wyświetlanie, kursor widoczny (Display ON, Cursor ON)
	0x0F – włącza wyświetlanie, kursor miga (Display ON, Cursor Blink)
	0x10 – przesuwa kursor w lewo (Cursor Shift Left)
	0x14 – przesuwa kursor w prawo (Cursor Shift Right)
	0x18 – przesuwa cały ekran w lewo (Display Shift Left)
	0x1C – przesuwa cały ekran w prawo (Display Shift Right)
	0x28 – tryb 4-bitowy, 2 linie, 5x8 punktów (Function Set)
	0x80 – kursor na początek pierwszej linii (Set DDRAM Address 0)
	0xC0 – kursor na początek drugiej linii (Set DDRAM Address 0x40)
 */

void LCD_SendData(uint8_t data){
	HAL_GPIO_WritePin(RS_GPIO_Port, RS_Pin, GPIO_PIN_SET); // RS = 1 -> TRYB DANYCH
	LCD_SendNibble(data >> 4);
	LCD_Confirm();
	LCD_SendNibble(data & 0x0F);
	LCD_Confirm();
	HAL_Delay(3);
}

void LCD_HD44780_Init(void){

	// Podobno tak jest w dokumentacji i tak zaleca producent :D
	HAL_Delay(50);

	LCD_SendNibble(0x03);
	LCD_Confirm();
	HAL_Delay(5);

	LCD_SendNibble(0x03);
	LCD_Confirm();
	HAL_Delay(5);

	LCD_SendNibble(0x03);
	LCD_Confirm();
	HAL_Delay(1);

	LCD_SendNibble(0x02);
	LCD_Confirm();
	HAL_Delay(1);
	// tutaj zalecnia sie koncza

	LCD_SendCMD(0x28); // tryb 4 bitowy
	LCD_SendCMD(0x0C);
	LCD_SendCMD(0x06);
	LCD_SendCMD(0x01);
	HAL_Delay(2);
}

void LCD_Clear(void){
	LCD_SendCMD(0x01);
	HAL_Delay(3);
}

void LCD_SetCursor(uint8_t row, uint8_t col){
	uint8_t addr = (row == 0)? col : (0x40 + col);
	// pierwsza linijka row = 0
	// druga linijka row = 1
	// pierwsza linia: adresy od 0x00 do 0x0F
	// druga linia: adresy od 0x40 do 0x4F
	// col oznacza znak, wartosc 8 bitowa - pelna rozdzielczosc 0-15 dziesietnie
	LCD_SendCMD(0x80 | addr); // ustawienie kursora w danym wierszu i danej kolumnie
}

void LCD_Print(char *str){// tutaj funkcja do wyswietlania wynikow

	while(*str){
		LCD_SendData(*str++);
	}
}
