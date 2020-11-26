# samd51-gpio_high_and_low_durations
This arduino code for SAMD51 will record a gpio's high and low duration for multiple pulses.
Each high and low duration will be captured up to 4 seconds in duration. Consecutive 200ns high and 200ns low pulses can be reliably captured when the SAMD51 is overclocked to 200MHz. I tested this on the Adafruit ItsyBity M4 board.
