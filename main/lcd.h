#ifndef LCD_H
#define LCD_H

void lcd_init(void);
void lcd_clear(void);
void lcd_gotoxy(int row, int col);
void lcd_print(const char *text);

void lcd_scroll_set_text(const char *text);
void lcd_scroll_show(void);
void lcd_scroll_up(void);
void lcd_scroll_down(void);

#endif // LCD_H
