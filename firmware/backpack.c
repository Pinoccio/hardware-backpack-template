#define F_CPU 128000UL // Define software reference clock for delay duration

// Must be write before call delay.h
#include <avr/io.h>
#include <util/delay.h>
 
#define SWT PB2 // Define ext switch pin on PB2 *1
#define LED PB0 // Define ext led pin on PB0 *2
 
int main(void) {
 
   DDRB &= ~(1<<SWT); // Set input direction on SWT (PB2) *1
   DDRB |= (1 << LED); // Set output direction on LED (PB0) *2
 
   for (;;) {
     if (bit_is_clear(PINB, SWT)) { // Read SWT pin (if SWT pressed, do the loop one time)
       PORTB &= ~(1 << LED); // Set 0 on LED pin (led turn on)
       _delay_ms(300); // Call delay for 300 milisec
       PORTB |= (1 << LED); // Set 1 on LED pin (led turn off)
       _delay_ms(300); // Call delay for 300 milisec
     }
   }
 
   return 0;
}