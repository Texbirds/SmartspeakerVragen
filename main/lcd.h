#ifndef LCD_H
#define LCD_H

void lcd_init(void);
void lcd_clear(void);
void lcd_gotoxy(int row, int col);
void lcd_print(const char *text);

#endif // LCD_H
