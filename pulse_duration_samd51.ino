// Many parts of this came from:
// https://forum.arduino.cc/index.php?topic=650931.0
//
// This program will count positive and negative edges
// It can reliably capture 200ns high pulse positive and negative edges
// If only one edge is wanted, it can capture 100ns pulse edges

#define SerialUSB         Serial // this is needed for trinket m0
#define PIN               2      // D2 is PA7, which is odd
#define CLK_FREQ          200.0  // update this to the core clock frequency, in MHz
#define SINGLE_CLK_PERIOD 5      // updates this to the core clock period, in ns
#define ARRAY_LENGTH      100    // number of consecutive high/low durations to be saved

volatile uint32_t cc0_array [ARRAY_LENGTH];
volatile uint32_t cc1_array [ARRAY_LENGTH];
volatile uint32_t isr_cc0_count;
volatile uint32_t isr_cc1_count;
volatile uint32_t isr_cc0_count_orig;
volatile uint32_t isr_cc1_count_orig;
volatile uint32_t a;
volatile uint32_t b;
volatile uint32_t high_duration;
volatile uint32_t low_duration;
volatile uint32_t array_length;
volatile char     serial_command;

void setup()
{
  SerialUSB.begin(115200);                       // Send data back on the native port
  while(!SerialUSB);                             // Wait for the SerialUSB port to be ready
  
  REG_MCLK_APBAMASK |= MCLK_APBAMASK_EIC  |     // Switch on the EIC peripheral 
                       MCLK_APBAMASK_TC0  |     // Switch on the TC0 peripheral
                       MCLK_APBAMASK_TC1;       // Switch on the TC1 peripheral

  REG_MCLK_APBBMASK |= MCLK_APBBMASK_EVSYS;      // Switch on the EVSYS for the Port event

  GCLK->PCHCTRL[TC0_GCLK_ID].reg = GCLK_PCHCTRL_CHEN |         // Enable perhipheral channel for TC0
                                   GCLK_PCHCTRL_GEN_GCLK0;     // Connect generic clock 0 at 120MHz

  // Enable the port multiplexer on pin number "PIN"
  PORT->Group[g_APinDescription[PIN].ulPort].PINCFG[g_APinDescription[PIN].ulPin].bit.PULLEN = 1; // out is default low so pull-down
  PORT->Group[g_APinDescription[PIN].ulPort].PINCFG[g_APinDescription[PIN].ulPin].bit.INEN   = 1;
  PORT->Group[g_APinDescription[PIN].ulPort].PINCFG[g_APinDescription[PIN].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[PIN].ulPort].PMUX[g_APinDescription[PIN].ulPin >> 1].reg |= PORT_PMUX_PMUXO(0); // 0 is Peripheral "A" EIC/EXTINT, PA07 is odd

  EIC->EVCTRL.reg       = 1 << 7;                     // Enable event output on external interrupt 7, must be 7 for PA07
  EIC->CONFIG[0].reg    = EIC_CONFIG_SENSE7_HIGH;     // Set event detecting a high
  EIC->INTENCLR.reg     = 1 << 7;                     // Clear the interrupt flag on channel 7
  EIC->CTRLA.bit.ENABLE = 1;                          // Enable EIC peripheral, no need to change CKSEL
  while (EIC->SYNCBUSY.bit.ENABLE);                   // Wait for synchronization

  // Select the event system user on channel 0 (USER number = channel number + 1)
  EVSYS->USER[EVSYS_ID_USER_TC0_EVU].reg = EVSYS_USER_CHANNEL(1);         // Set the event user (receiver) as timer TC0


  EVSYS->Channel[0].CHANNEL.reg = EVSYS_CHANNEL_EDGSEL_NO_EVT_OUTPUT |            // No event edge detection, we already have it on the EIC
                                  EVSYS_CHANNEL_PATH_ASYNCHRONOUS    |            // Set event path as asynchronous
                                  EVSYS_CHANNEL_EVGEN(EVSYS_ID_GEN_EIC_EXTINT_7); // Set event generator (sender) as external interrupt 7   
  
  REG_TC0_EVCTRL |= TC_EVCTRL_TCEI |              // Enable the TC event input
                    TC_EVCTRL_EVACT_PWP;          // Set up the timer for capture: CC0 high pulsewidth, CC1 period

  REG_TC0_CTRLA |= TC_CTRLA_CAPTEN1 |             // Enable event capture on CC1
                   TC_CTRLA_CAPTEN0;              // Enable event capture on CC0

  NVIC_SetPriority(TC0_IRQn, 0);                 // Set Nested Vector Interrupt Controller (NVIC) priority for TC0 to 0 (highest)
  NVIC_EnableIRQ(TC0_IRQn);    
  
  isr_cc0_count = 0;
  isr_cc1_count = 0;

  REG_TC0_INTENSET |= TC_INTENSET_MC1 |           // Enable compare channel 1 (CC1) interrupts
                      TC_INTENSET_MC0;            // Enable compare channel 0 (CC0) interrupts
 
  REG_TC0_CTRLA |= TC_CTRLA_PRESCALER_DIV1 |     // Set prescaler to 1, 48MHz/1 = 48MHz
                   TC_CTRLA_MODE_COUNT32   |     // Set the TC0 timer to 32-bit mode in conjuction with timer TC1
                   TC_CTRLA_ENABLE;              // Enable TC0
  while (TC0->COUNT32.SYNCBUSY.bit.ENABLE);          // Wait for synchronization
}

void loop()
{
  
  if (SerialUSB.available())
  {
    noInterrupts();
    serial_command = SerialUSB.read();
    if (serial_command == 'c')
    {
      isr_cc0_count = 0;
      isr_cc1_count = 0;
    }
    else
    { 
      isr_cc0_count_orig = isr_cc0_count;
      isr_cc1_count_orig = isr_cc1_count;
      if (isr_cc0_count > 0) // this code is to remove overflow counts
      {
        array_length = isr_cc0_count - 1;
        for ( a=0; a < array_length; a = a + 1 )
        {
          high_duration = (cc0_array[a] * (1000.0 / CLK_FREQ) + SINGLE_CLK_PERIOD);
          if (high_duration > 3900000000) // values higher than this are probably overflow
            {
              isr_cc0_count--;
              for ( b=a; b < array_length; b = b + 1 )
              {
                cc0_array[b] = cc0_array[b+1]; // remove bad array vale
              }
            }
        }
      }
      if (isr_cc1_count > 0)
      {
        array_length = isr_cc1_count - 1;
        for ( a=0; a < array_length; a = a + 1 )
        {
          low_duration = (cc1_array[a] * (1000.0 / CLK_FREQ) + SINGLE_CLK_PERIOD);
          if (low_duration > 3900000000)
          {
            isr_cc1_count--;
            for ( b=a; b < array_length; b = b + 1 )
            {
              cc1_array[b] = cc1_array[b+1];
            }
          }
        }
      }
      SerialUSB.print("pos_pulses_count=");
      SerialUSB.println(isr_cc0_count_orig);
      SerialUSB.print("neg_pulses_count=");
      SerialUSB.println(isr_cc1_count_orig);

      for ( a=0; a < isr_cc0_count; a = a + 1 )
      {
        high_duration = cc0_array[a] * (1000.0 / CLK_FREQ) + SINGLE_CLK_PERIOD;
        SerialUSB.print("high_duration=");
        SerialUSB.println(high_duration);
        if (a < isr_cc1_count)
        {
          low_duration = (cc1_array[a] * (1000.0 / CLK_FREQ) + SINGLE_CLK_PERIOD) - high_duration;
          SerialUSB.print("low_duration=");
          SerialUSB.println(low_duration);
        }
      }
      SerialUSB.print("--> input c to clear counts <--\n");
      interrupts();
    }
  }
}

// overflow is not checked to allow this to run as fast as possible
void TC0_Handler()   // Interrupt Service Routine (ISR) for timer TC0
{
  // Check for match counter 0 (MC0) interrupt
  if (TC0->COUNT32.INTFLAG.bit.MC0)
  {
     REG_TC0_INTFLAG = TC_INTFLAG_MC0;
     cc0_array[isr_cc0_count++] = REG_TC0_COUNT32_CC0;
  }
  // Check for match counter 1 (MC1) interrupt
  if (TC0->COUNT32.INTFLAG.bit.MC1)
  {
     REG_TC0_INTFLAG = TC_INTFLAG_MC1;
     cc1_array[isr_cc1_count++] = REG_TC0_COUNT32_CC1;
  }
}
