#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <math.h>
#include <stdio.h>
#include <avr/dtostrf.h>
#include <pico/mutex.h>
#include <ZeroAPRS.h>                       //https://github.com/hakkican/ZeroAPRS
#if defined(ARDUINO_ARCH_SAMD)
#include <ZeroSi4463.h>                     //https://github.com/hakkican/ZeroSi4463
#endif
#include <TinyGPS++.h>                      //https://github.com/mikalhart/TinyGPSPlus
#include <GEOFENCE.h>                       // Modified version of https://github.com/TomasTT7/TT7F-Float-Tracker/blob/master/Software/ARM_GEOFENCE.c
#include <Adafruit_SleepyDog.h>             //https://github.com/adafruit/Adafruit_SleepyDog
#if defined(ARDUINO_ARCH_SAMD)
#include <Adafruit_BMP085.h>                //https://github.com/adafruit/Adafruit-BMP085-Library
#include <si5351.h>                         //https://github.com/etherkit/Si5351Arduino
#endif
#include <JTEncode.h>                       //https://github.com/etherkit/JTEncode (JT65/JT9/JT4/FT8/WSPR/FSQ Encoder Library)
#include <TimeLib.h>                        //https://github.com/PaulStoffregen/Time
#if defined(ARDUINO_ARCH_SAMD)
#include <Adafruit_ZeroTimer.h>             //https://github.com/adafruit/Adafruit_ZeroTimer
#include <MemoryFree.h>;
#elif defined(ARDUINO_ARCH_RP2040)
#include <MemoryFree.h>
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#endif

uint32_t core1_counter = 0U;
mutex_t core1_counter_mutex;

#if defined(ARDUINO_ARCH_SAMD)

#define Si5351Pwr     A3
#define TCXO_Pwr      A4
#define BattPin       A5
#define PTT_PIN       3
#define GpsPwr        7
#define Si446x_nIRQ   9
#define Si446x_SDN    4
#define Si446x_nSEL   8

//macros
#define Si5351ON    digitalWrite(Si5351Pwr, HIGH)//NPN
#define Si5351OFF   digitalWrite(Si5351Pwr, LOW)
#define GpsON       digitalWrite(GpsPwr, LOW)//PNP
#define GpsOFF      digitalWrite(GpsPwr, HIGH);
#define PttON       digitalWrite(PTT_PIN, HIGH)
#define PttOFF      digitalWrite(PTT_PIN, LOW)
#define Si4463ON    digitalWrite(Si446x_SDN, LOW)
#define Si4463OFF   digitalWrite(Si446x_SDN, HIGH)
#define TcxoON      digitalWrite(TCXO_Pwr, HIGH)
#define TcxoOFF     digitalWrite(TCXO_Pwr, LOW)

#elif defined(ARDUINO_ARCH_RP2040)

#define SerialUSB   Serial

#define Si5351Pwr     4
#define BattPin       A3
#define GpsPwr        16

//macros
#define Si5351ON    
#define Si5351OFF   vfo_turn_off()
#define GpsON                   \
  do {                          \
    Serial2.begin(9600);        \
    digitalWrite(GpsPwr, LOW);  \
    /*printf("GpsON\n");*/      \
  } while (false)
#define GpsOFF                  \
  do {                          \
    digitalWrite(GpsPwr, HIGH); \
    Serial2.end();              \
    /*printf("GpsOFF\n");*/     \
  } while (false)
#define PttON       
#define PttOFF      
#define Si4463ON    
#define Si4463OFF   
#define TcxoON      
#define TcxoOFF     

#endif

//#define DEVMODE // Development mode. Uncomment to enable for debugging.
//#define DEVMODE2// Development mode. Uncomment to enable for debugging.

//******************************  APRS CONFIG **********************************
char    CallSign[7]="NOCALL";//DO NOT FORGET TO CHANGE YOUR CALLSIGN
int8_t  CallNumber=11; //11; //SSID http://www.aprs.org/aprs11/SSIDs.txt
char    Symbol='O'; // 'O' for balloon, '>' for car, for more info : http://www.aprs.org/symbols/symbols-new.txt
bool    alternateSymbolTable = false ; //false = '/' , true = '\'

char    comment[50] = "LightAPRS-W 2.0";// Max 50 char
mutex_t commentMutex;
char    StatusMessage[50] = "LightAPRS-W 2.0 by TA2NHP & TA2MUN";
mutex_t StatusMessageMutex;

uint32_t GEOFENCE_APRS_frequency      = 144390000 ;//default frequency before geofencing. This variable will be updated based on GPS location.
int32_t APRS_Freq_Correction          = -1200;     //Hz. vctcxo frequency correction for si4463

//*****************************************************************************

uint16_t  BeaconWait=50;  //seconds sleep for next beacon (HF or VHF). This is an optimized value, do not change this if possible.
uint16_t  BattWait=60;    //seconds sleep if super capacitors/batteries are below BattMin (important if power source is solar panel) 
float     BattMin=3.3;    // min Volts to wake up.
float     GpsMinVolt=4.5; //min Volts for GPS to wake up. (important if power source is solar panel) 
float     WsprBattMin=4.0;//min Volts for HF (WSPR) radio module to transmit (TX) ~10 mW
float     HighVolt=6.0; //GPS is always on if the voltage exceeds this value to protect solar caps from overcharge

//******************************  HF (WSPR) CONFIG *************************************

char hf_call[7] = "NOCALL";// DO NOT FORGET TO CHANGE YOUR CALLSIGN

//#define WSPR_DEFAULT_FREQ       10140200UL //30m band
#define WSPR_DEFAULT_FREQ       14097100UL //20m band
//#define WSPR_DEFAULT_FREQ       18106100UL //17M band
//#define WSPR_DEFAULT_FREQ       21096100UL //15m band
//#define WSPR_DEFAULT_FREQ       24926100UL //12M band
//#define WSPR_DEFAULT_FREQ       28126100UL //10m band
//for all bands -> http://wsprnet.org/drupal/node/7352

#if defined(ARDUINO_ARCH_RP2040)
bool core1_separate_stack = true;
#endif


// Supported modes, default HF mode is WSPR
enum mode {MODE_JT9, MODE_JT65, MODE_JT4, MODE_WSPR, MODE_FSQ_2, MODE_FSQ_3,
  MODE_FSQ_4_5, MODE_FSQ_6, MODE_FT8};
  
enum mode cur_mode = MODE_WSPR; //default HF mode

//supported other modes freq for 20m
#define JT9_DEFAULT_FREQ        14078700UL
#define FT8_DEFAULT_FREQ        14075000UL
#define JT65_DEFAULT_FREQ       14078300UL
#define JT4_DEFAULT_FREQ        14078500UL
#define FSQ_DEFAULT_FREQ        7105350UL     // Base freq is 1350 Hz higher than dial freq in USB

//*******************************************************************************


//******************************  APRS SETTINGS *********************************

//do not change WIDE path settings below if you don't know what you are doing :) 
uint8_t   Wide1=1; // 1 for WIDE1-1 path
uint8_t   Wide2=1; // 1 for WIDE2-1 path

/**
Airborne stations above a few thousand feet should ideally use NO path at all, or at the maximum just WIDE2-1 alone.  
Due to their extended transmit range due to elevation, multiple digipeater hops are not required by airborne stations.  
Multi-hop paths just add needless congestion on the shared APRS channel in areas hundreds of miles away from the aircraft's own location.  
NEVER use WIDE1-1 in an airborne path, since this can potentially trigger hundreds of home stations simultaneously over a radius of 150-200 miles. 
 */
uint8_t pathSize=2; // 2 for WIDE1-N,WIDE2-N ; 1 for WIDE2-N
boolean autoPathSizeHighAlt = true; //force path to WIDE2-N only for high altitude (airborne) beaconing (over 1.000 meters (3.280 feet)) 
boolean beaconViaARISS = true; //there are no iGates in some regions (such as North Africa,  Oceans, etc) so try to beacon via ARISS (International Space Station) https://www.amsat.org/amateur-radio-on-the-iss/

// Send aprs high precision position extension (adds 5 bytes to beacon messaage)
boolean send_aprs_enhanced_precision = true;
boolean  aliveStatus = true; //for tx status message on first wake-up just once.
boolean radioSetup = false; //do not change this, temp value
static char telemetry_buff[100];// telemetry buffer
uint16_t TxCount = 1; //increase +1 after every APRS transmission

//*******************************************************************************

//******************************  HF SETTINGS   *********************************

#define JT9_TONE_SPACING        174          // ~1.74 Hz
#define JT65_TONE_SPACING       269          // ~2.69 Hz
#define JT4_TONE_SPACING        437          // ~4.37 Hz
#define WSPR_TONE_SPACING       146          // ~1.46 Hz
#define FSQ_TONE_SPACING        879          // ~8.79 Hz
#define FT8_TONE_SPACING        625          // ~6.25 Hz

#define JT9_DELAY               576          // Delay value for JT9-1
#define JT65_DELAY              371          // Delay in ms for JT65A
#define JT4_DELAY               229          // Delay value for JT4A
#define WSPR_DELAY              683          // Delay value for WSPR
#define FSQ_2_DELAY             500          // Delay value for 2 baud FSQ
#define FSQ_3_DELAY             333          // Delay value for 3 baud FSQ
#define FSQ_4_5_DELAY           222          // Delay value for 4.5 baud FSQ
#define FSQ_6_DELAY             167          // Delay value for 6 baud FSQ
#define FT8_DELAY               159          // Delay value for FT8

#define HF_CORRECTION              -13000       // Change this for your ref osc
  
// Global variables
unsigned long hf_freq;
char hf_message[13] = "NOCALL AA00";//for non WSPR modes, you don't have to change this, updated by hf_call and GPS location
char hf_loc[] = "AA00";             //for WSPR, updated by GPS location. You don't have to change this.
uint8_t dbm = 10;
uint8_t tx_buffer[255];
uint8_t symbol_count;
uint16_t tone_delay, tone_spacing;
volatile bool proceed = false;


//*******************************************************************************


//******************************  GPS SETTINGS   *********************************
int16_t   GpsResetTime=1800; // timeout for reset if GPS is not fixed

// GEOFENCE 
uint32_t GEOFENCE_no_tx               = 0; //do not change this, temp value. 
boolean arissModEnabled = false; //do not change this, temp value. 

boolean GpsFirstFix=false; //do not change this
boolean ublox_high_alt_mode_enabled = false; //do not change this
int16_t GpsInvalidTime=0; //do not change this

//********************************************************************************

#if defined(ARDUINO_ARCH_SAMD)
Si5351 si5351(0x60);
#endif
TinyGPSPlus gps;
#if defined(ARDUINO_ARCH_SAMD)
Adafruit_BMP085 bmp;
#endif
JTEncode jtencode;
#if defined(ARDUINO_ARCH_SAMD)
Si4463 si4463(Si446x_nIRQ, Si446x_SDN, Si446x_nSEL);
Adafruit_ZeroTimer zerotimer = Adafruit_ZeroTimer(3);
#endif


#if defined(ARDUINO_ARCH_RP2040)

#define WSPR_TX_CLK_NUM     1
#define APRS_TX_CLK_NUM     0

#define GPS_VCC_ON_N_PIN            16
#define GPS_NRESET_PIN              5
#define GPS_ON_PIN                  6
#define GPS_UART1_TX_PIN            8
#define GPS_UART1_RX_PIN            9
#define GPS_1PPS_PIN                17

#define VFO_VDD_ON_N_PIN            4
#define VFO_I2C0_SDA_PIN            12
#define VFO_I2C0_SCL_PIN            13

#define BMP280_I2C1_SDA_PIN         2
#define BMP280_I2C1_SCL_PIN         3

#define PLL_CALCULATION_PRECISION   4

#define SI5351A_CLK_IDRV_8MA        (3 << 0)
#define SI5351A_CLK_IDRV_6MA        (2 << 0)
#define SI5351A_CLK_IDRV_4MA        (1 << 0)
#define SI5351A_CLK_IDRV_2MA        (0 << 0)

#define VFO_I2C_INSTANCE            i2c0
#define VFO_I2C0_SCL_HZ             (1000 * 1000)

uint8_t aprs_si5351_clk_port = APRS_TX_CLK_NUM;

static void vfo_init(void)
{
  // turn ON VFO VDD
  gpio_init(VFO_VDD_ON_N_PIN);
  gpio_pull_up(VFO_VDD_ON_N_PIN);
  gpio_put(VFO_VDD_ON_N_PIN, 0);

  // init I2C0 for VFO
  i2c_init(VFO_I2C_INSTANCE, VFO_I2C0_SCL_HZ);

  gpio_set_pulls(VFO_I2C0_SDA_PIN, false, false);
  gpio_set_pulls(VFO_I2C0_SCL_PIN, false, false);

  gpio_set_function(VFO_I2C0_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(VFO_I2C0_SCL_PIN, GPIO_FUNC_I2C);
}

static void vfo_set_power_on(bool turn_on)
{
  static bool s_is_on = false;
  if (turn_on == s_is_on) return;
  s_is_on = turn_on;
  gpio_set_dir(VFO_VDD_ON_N_PIN, (turn_on ? GPIO_OUT : GPIO_IN));
}

#define SI5351A_I2C_ADDR                0x60

static uint8_t s_i2c_buf[16];

int i2cWrite(uint8_t reg, uint8_t val){    // write reg via i2c
  s_i2c_buf[0] = reg;
  s_i2c_buf[1] = val;

  int res;
  res = i2c_write_timeout_us(VFO_I2C_INSTANCE, SI5351A_I2C_ADDR, s_i2c_buf, 2, false, 1000);

  if (res < PICO_ERROR_NONE) {
    #if defined(DEVMODE)
    printf("I2C error %d: reg:%02x val:%02x\n", res, reg, val);
    #endif
  }
  return res;
}

int i2cWriten(uint8_t reg, uint8_t *vals, uint8_t vcnt){   // write array
  s_i2c_buf[0] = reg;
  memcpy(&s_i2c_buf[1], vals, vcnt);

  int res;
  res = i2c_write_timeout_us(VFO_I2C_INSTANCE, SI5351A_I2C_ADDR, s_i2c_buf, (vcnt + 1), false, 10000);

  if (res < PICO_ERROR_NONE) {
    #if defined(DEVMODE)
    printf("I2C error %d: reg:%02x\n", res, reg);
    #endif
  }
  return res;
}

/*
    Si5351A related functions
    Developed by Kazuhisa "Kazu" Terasaki AG6NS
    https://github.com/kaduhi/AFSK_to_FSK_VFO
    This code was developed originally for QRPGuys AFP-FSK Digital Transceiver III kit
    https://qrpguys.com/qrpguys-digital-fsk-transceiver-iii
    https://qrpguys.com/wp-content/uploads/2022/09/ft8_v1.4_092522-1.zip
 */

#define SI5351_TCXO_FREQ                26000000

#define SI5351A_OUTPUT_ENABLE_CONTROL   3
#define SI5351A_CLK0_CONTROL            16
#define SI5351A_CLK1_CONTROL            17
#define SI5351A_CLK7_CONTROL            23
#define SI5351A_PLLB_BASE               34
#define SI5351A_MULTISYNTH0_BASE        42
#define SI5351A_MULTISYNTH1_BASE        50
#define SI5351A_PLL_RESET               177

#define SI5351A_CLK0_MS0_INT            (1 << 6)
#define SI5351A_CLK0_MS0_SRC_PLLB       (1 << 5)
#define SI5351A_CLK0_SRC_MULTISYNTH_0   (3 << 2)
#define SI5351A_CLK0_IDRV_8MA           (3 << 0)
#define SI5351A_CLK0_IDRV_6MA           (2 << 0)
#define SI5351A_CLK0_IDRV_4MA           (1 << 0)
#define SI5351A_CLK0_IDRV_2MA           (0 << 0)

#define SI5351A_CLK1_MS1_INT            (1 << 6)
#define SI5351A_CLK1_MS1_SRC_PLLB       (1 << 5)
#define SI5351A_CLK1_CLK1_INV           (1 << 4)
#define SI5351A_CLK1_SRC_MULTISYNTH_0   (2 << 2)
#define SI5351A_CLK1_SRC_MULTISYNTH_1   (3 << 2)
#define SI5351A_CLK1_IDRV_8MA           (3 << 0)
#define SI5351A_CLK1_IDRV_6MA           (2 << 0)
#define SI5351A_CLK1_IDRV_4MA           (1 << 0)
#define SI5351A_CLK1_IDRV_2MA           (0 << 0)

#define SI5351A_PLL_RESET_PLLB_RST      (1 << 7)

static uint32_t prev_ms_div = 0;
static uint8_t s_regs[8];

static uint8_t s_vfo_drive_strength[3];  // 0:2mA, 1:4mA, 2:6mA, 3:8mA

void si5351a_setup_PLLB(uint8_t mult, uint32_t num, uint32_t denom)
{
  uint32_t p1 = 128 * mult + ((128 * num) / denom) - 512;
  uint32_t p2 = 128 * num - denom * ((128 * num) / denom);
  uint32_t p3 = denom;

  s_regs[0] = (uint8_t)(p3 >> 8);
  s_regs[1] = (uint8_t)p3;
  s_regs[2] = (uint8_t)(p1 >> 16) & 0x03;
  s_regs[3] = (uint8_t)(p1 >> 8);
  s_regs[4] = (uint8_t)p1;
  s_regs[5] = ((uint8_t)(p3 >> 12) & 0xf0) | ((uint8_t)(p2 >> 16) & 0x0f);
  s_regs[6] = (uint8_t)(p2 >> 8);
  s_regs[7] = (uint8_t)p2;
#if 0
  i2cWriten(SI5351A_PLLB_BASE, s_regs, 8);
#else
  static uint8_t s_regs_prev[8];
  uint8_t start = 0;
  uint8_t end = 7;
  if (prev_ms_div != 0) {
    for (; start < 8; start++) {
      if (s_regs[start] != s_regs_prev[start]) break;
    }
    if (start == 8) return;
    for (; end > start; end--) {
      if (s_regs[end] != s_regs_prev[end]) break;
    }
  }
  uint8_t reg = SI5351A_PLLB_BASE + start;
  uint8_t len = end - start + 1;
  i2cWriten(reg, &s_regs[start], len);
  *((uint64_t *)s_regs_prev) = *((uint64_t *)s_regs);
#endif
}

// div must be even number
void si5351a_setup_multisynth0(uint32_t div)
{
  uint32_t p1 = 128 * div - 512;

  s_regs[0] = 0;
  s_regs[1] = 1;
  s_regs[2] = (uint8_t)(p1 >> 16) & 0x03;
  s_regs[3] = (uint8_t)(p1 >> 8);
  s_regs[4] = (uint8_t)p1;
  s_regs[5] = 0;
  s_regs[6] = 0;
  s_regs[7] = 0;
  i2cWriten(SI5351A_MULTISYNTH0_BASE, s_regs, 8);

  i2cWrite(SI5351A_CLK0_CONTROL, (SI5351A_CLK0_MS0_INT | 
                                  SI5351A_CLK0_MS0_SRC_PLLB | 
                                  SI5351A_CLK0_SRC_MULTISYNTH_0 | 
                                  s_vfo_drive_strength[0]));
#ifdef TEST_ONLY
  printf("VFO_DRIVE_STRENGTH: %d\n", (int)s_vfo_drive_strength[0]);
#endif //TEST_ONLY

#ifdef ENABLE_DIFFERENTIAL_TX_OUTPUT
  i2cWriten(SI5351A_MULTISYNTH1_BASE, s_regs, 8);

  i2cWrite(SI5351A_CLK1_CONTROL, (SI5351A_CLK1_MS1_INT | 
                                  SI5351A_CLK1_MS1_SRC_PLLB | 
                                  SI5351A_CLK1_CLK1_INV | 
                                  SI5351A_CLK1_SRC_MULTISYNTH_1 | 
                                  s_vfo_drive_strength[0]));
#endif
}

static void si5351a_setup_multisynth1(uint32_t div)
{
  uint32_t p1 = 128 * div - 512;

  s_regs[0] = 0;
  s_regs[1] = 1;
  s_regs[2] = (uint8_t)(p1 >> 16) & 0x03;
  s_regs[3] = (uint8_t)(p1 >> 8);
  s_regs[4] = (uint8_t)p1;
  s_regs[5] = 0;
  s_regs[6] = 0;
  s_regs[7] = 0;
  i2cWriten(SI5351A_MULTISYNTH1_BASE, s_regs, 8);

  i2cWrite(SI5351A_CLK1_CONTROL, (SI5351A_CLK1_MS1_INT | 
                                  SI5351A_CLK1_MS1_SRC_PLLB | 
                                  SI5351A_CLK1_SRC_MULTISYNTH_1 | 
                                  s_vfo_drive_strength[1]));
#ifdef TEST_ONLY
  printf("VFO_DRIVE_STRENGTH: %d\n", (int)s_vfo_drive_strength[1]);
#endif //TEST_ONLY
}

static void si5351a_reset_PLLB(void)
{
  i2cWrite(SI5351A_PLL_RESET, SI5351A_PLL_RESET_PLLB_RST);
}

// freq is in 28.4 fixed point number, 0.0625Hz resolution
void vfo_set_freq_x16(uint8_t clk_number, uint32_t freq)
{
  #define PLL_MAX_FREQ        900000000
  #define PLL_MIN_FREQ        600000000
  #define PLL_MID_FREQ        ((PLL_MAX_FREQ + PLL_MIN_FREQ) / 2)
  #define PLL_DENOM_MAX       0x000fffff

  uint32_t ms_div = PLL_MID_FREQ / (freq >> PLL_CALCULATION_PRECISION) + 1;
  ms_div &= 0xfffffffe;   // make it even number

  uint32_t pll_freq = ((uint64_t)freq * ms_div) >> PLL_CALCULATION_PRECISION;

  uint32_t tcxo_freq = SI5351_TCXO_FREQ;
  uint32_t pll_mult   = pll_freq / tcxo_freq;
  uint32_t pll_remain = pll_freq - (pll_mult * tcxo_freq);
  uint32_t pll_num    = (uint64_t)pll_remain * PLL_DENOM_MAX / tcxo_freq;
  si5351a_setup_PLLB(pll_mult, pll_num, PLL_DENOM_MAX);

  if (ms_div != prev_ms_div) {
    prev_ms_div = ms_div;
    if (clk_number == 0) {
      si5351a_setup_multisynth0(ms_div);
    }
    else {
      si5351a_setup_multisynth1(ms_div);
    }
    si5351a_reset_PLLB();
  }
}

static uint8_t  si5351bx_clken = 0xff;

void vfo_turn_on_clk_out(uint8_t clk_number)
{
  uint8_t enable_bit = 1 << clk_number;
#ifdef ENABLE_DIFFERENTIAL_TX_OUTPUT
  if (clk_number == 0) {
    enable_bit |= 1 << 1;
  }
#endif
  si5351bx_clken &= ~enable_bit;
  i2cWrite(SI5351A_OUTPUT_ENABLE_CONTROL, si5351bx_clken);
}

void vfo_turn_off_clk_out(uint8_t clk_number)
{
  uint8_t enable_bit = 1 << clk_number;
#ifdef ENABLE_DIFFERENTIAL_TX_OUTPUT
  if (clk_number == 0) {
    enable_bit |= 1 << 1;
  }
#endif
  si5351bx_clken |= enable_bit;
  i2cWrite(SI5351A_OUTPUT_ENABLE_CONTROL, si5351bx_clken);
}

void vfo_set_drive_strength(uint8_t clk_number, uint8_t strength)
{
  s_vfo_drive_strength[clk_number] = strength;

  // reset the prev_ms_div to force vfo_set_freq_x16() to call si5351a_setup_multisynth1() next time
  prev_ms_div = 0;
}

bool vfo_is_on(void)
{
  return gpio_is_dir_out(VFO_VDD_ON_N_PIN);
}

void vfo_turn_on(uint8_t clk_number)
{
  if (vfo_is_on()) return;    // already on

  gpio_set_function(VFO_I2C0_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(VFO_I2C0_SCL_PIN, GPIO_FUNC_I2C);
  vfo_set_power_on(true);
  // sleep_ms(100);
  busy_wait_us_32(100000);

  // output 7MHz on CLK0
  uint8_t reg;
  while (i2cWrite(SI5351A_OUTPUT_ENABLE_CONTROL, 0xff) < PICO_ERROR_NONE) {   // Disable all CLK output drivers
    i2c_deinit(VFO_I2C_INSTANCE);
    // sleep_ms(10);
    busy_wait_us_32(10000);
    i2c_init(VFO_I2C_INSTANCE, VFO_I2C0_SCL_HZ);
    gpio_set_pulls(VFO_I2C0_SDA_PIN, false, false);
    gpio_set_pulls(VFO_I2C0_SCL_PIN, false, false);
    gpio_set_function(VFO_I2C0_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(VFO_I2C0_SCL_PIN, GPIO_FUNC_I2C);
    // sleep_ms(10);
    busy_wait_us_32(10000);
  }
  for (reg = SI5351A_CLK0_CONTROL; reg <= SI5351A_CLK7_CONTROL; reg++) i2cWrite(reg, 0xCC);    // Powerdown CLK's

  static const uint8_t s_ms_values[] = { 0, 1, 0x0C, 0, 0, 0, 0, 0 };
  i2cWriten(42, (uint8_t *)s_ms_values, 8);   // set MS0 for div_4 mode (minimum division)
  i2cWriten(50, (uint8_t *)s_ms_values, 8);   // set MS1 for div_4 mode (minimum division)
  i2cWriten(58, (uint8_t *)s_ms_values, 8);   // set MS2 for div_4 mode (minimum division)

  static const uint8_t s_pll_values[] = { 0, 0, 0, 0x05, 0x00, 0, 0, 0 };
  i2cWriten(26, (uint8_t *)s_pll_values, 8);  // set PLLA for div_16 mode (minimum even integer division)
  i2cWriten(34, (uint8_t *)s_pll_values, 8);  // set PLLB for div_16 mode (minimum even integer division)

  i2cWrite(149, 0x00);  // Disable Spread Spectrum
  i2cWrite(177, 0xA0);  // Reset PLLA and PLLB
  i2cWrite(187, 0x00);  // Disable all fanout

  prev_ms_div = 0;

  // uint32_t freq = 7040000UL << PLL_CALCULATION_PRECISION;
  uint32_t freq = 14097000UL << PLL_CALCULATION_PRECISION;
  vfo_set_freq_x16(clk_number, freq);

  si5351bx_clken = 0xff;
  vfo_turn_on_clk_out(clk_number);
}

void vfo_turn_off(void)
{
  if (!vfo_is_on()) return;   // already off

  // disable all clk output
  si5351bx_clken = 0xff;
  i2cWrite(SI5351A_OUTPUT_ENABLE_CONTROL, si5351bx_clken);
  // sleep_ms(10);
  busy_wait_us_32(10000);

  vfo_set_power_on(false);
  gpio_set_function(VFO_I2C0_SDA_PIN, GPIO_FUNC_NULL);
  gpio_set_function(VFO_I2C0_SCL_PIN, GPIO_FUNC_NULL);
}


/*
  status indicator LED
*/

#define STATUS_LED_PIN              25

#define turnOnLED(turn_on)          digitalWrite(STATUS_LED_PIN, (turn_on) ? HIGH : LOW)
#define isLEDOn()                   (digitalRead(STATUS_LED_PIN) ? true : false)
#define flipLED()                   turnLedOn(!isLedOn())

#define LED_BLINK_ON_PERIOD_USEC    50000
#define LED_BLINK_OFF_PERIOD_USEC   300000
#define LED_BLINK_PAUSE_PERIOD_USEC 1000000

#define LED_STATUS_NO_GPS           1
#define LED_STATUS_GPS_TIME         2
#define LED_STATUS_GPS_FIX          3
#define LED_STATUS_TX_APRS          4
#define LED_STATUS_TX_WSPR          5

int statusLEDBlinkCnt = 0;

void initStatusLED(void)
{
  pinMode(STATUS_LED_PIN, OUTPUT);
  turnOnLED(true);
}

void setStatusLEDBlinkCount(int cnt)
{
  statusLEDBlinkCnt = cnt;
}

void updateStatusLED(void)
{
  static uint32_t nextFlipUsec = 0;
  static int targetBlinkCnt = 0;
  static int currBlinkCnt = 0;

  uint32_t usec = time_us_32();
  if ((int32_t)(nextFlipUsec - usec) <= 0) {
    if (isLEDOn() == false) {
      // OFF to ON
      if (targetBlinkCnt == 0) {
        targetBlinkCnt = statusLEDBlinkCnt;
        currBlinkCnt = 0;
      }
      if (++currBlinkCnt <= targetBlinkCnt) {
        turnOnLED(true);
      }
      nextFlipUsec = usec + LED_BLINK_ON_PERIOD_USEC;
    }
    else {
      // ON to OFF
      turnOnLED(false);
      if (currBlinkCnt >= targetBlinkCnt) {
        nextFlipUsec = usec + LED_BLINK_PAUSE_PERIOD_USEC;
        targetBlinkCnt = 0;
      }
      else {
        nextFlipUsec = usec + LED_BLINK_OFF_PERIOD_USEC;
      }
    }
  }
}

#endif


void setup() {
  mutex_init(&core1_counter_mutex);
  mutex_init(&commentMutex);
  mutex_init(&StatusMessageMutex);
  Watchdog.enable(30000);
  Watchdog.reset();
  // While the energy rises slowly with the solar panel, 
  // using the analog reference low solves the analog measurement errors.
#if defined(ARDUINO_ARCH_SAMD)
  analogReference(AR_INTERNAL1V65);
  pinMode(PTT_PIN, OUTPUT);
  pinMode(Si5351Pwr, OUTPUT);
  pinMode(TCXO_Pwr, OUTPUT);
  pinMode(GpsPwr, OUTPUT);
  pinMode(BattPin, INPUT);
  pinMode(Si446x_SDN, OUTPUT);
#elif defined(ARDUINO_ARCH_RP2040)
  initStatusLED();
  setStatusLEDBlinkCount(LED_STATUS_NO_GPS);
  // pinMode(Si5351Pwr, OUTPUT);
  pinMode(GpsPwr, OUTPUT);
  // pinMode(BattPin, INPUT);
  analogReadResolution(12);
#endif
  
  GpsOFF;
  Si5351OFF;  
  PttOFF;
  Si4463OFF;
  TcxoOFF;

#if defined(ARDUINO_ARCH_SAMD)
  Serial.begin(9600);//GPS
#elif defined(ARDUINO_ARCH_RP2040)
  Serial2.setRX(GPS_UART1_RX_PIN);
  Serial2.setTX(GPS_UART1_TX_PIN);
  Serial2.begin(9600);//GPS
#endif
  SerialUSB.begin(115200);
  // Wait up to 5 seconds for serial to be opened, to allow catching
  // startup messages on native USB boards (that do not reset when
  // serial is opened).
  Watchdog.reset();  
  unsigned long start = millis();
  while (millis() - start < 5000 && !SerialUSB){;}
  Watchdog.reset(); 

  SerialUSB.println(F("Starting"));

  APRS_init();
  APRS_setCallsign(CallSign, CallNumber);
  APRS_setDestination("APLIGA", 0);
  APRS_setPath1("WIDE1", Wide1);
  APRS_setPath2("WIDE2", Wide2);
  APRS_setPathSize(2);
  APRS_useAlternateSymbolTable(alternateSymbolTable);
  APRS_setSymbol(Symbol);
  APRS_setPathSize(pathSize);

#if defined(ARDUINO_ARCH_SAMD)
  Wire.begin();
  bmp.begin();
#elif defined(ARDUINO_ARCH_RP2040)
  Wire.begin(); // somehow this is necessary for Serial2 to work properly
  vfo_init();
#endif

  SerialUSB.println(F(""));
  SerialUSB.print(F("APRS (VHF) CallSign: "));
  SerialUSB.print(CallSign);
  SerialUSB.print(F("-"));
  SerialUSB.println(CallNumber);

  SerialUSB.print(F("WSPR (HF) CallSign: "));
  SerialUSB.println(hf_call);
  SerialUSB.println(F(""));
  
}

void loop() {
  Watchdog.reset();


#if defined(ARDUINO_ARCH_RP2040)
  updateStatusLED();
#endif

  if (((readBatt() > BattMin) && GpsFirstFix) || ((readBatt() > GpsMinVolt) && !GpsFirstFix)) {

    if (aliveStatus) {

      sendStatus();	  
      aliveStatus = false;

      while (readBatt() < BattMin) {
        sleepSeconds(BattWait); 
      }   
    }

      uint32_t mutex_owner_id;
      bool hasMutex = mutex_try_enter(&core1_counter_mutex, &mutex_owner_id);
      if (hasMutex) {
          SerialUSB.println("");
          SerialUSB.print("DEBUG DEBUG: Got from core1 value: ");
          SerialUSB.println(core1_counter);
          SerialUSB.println("");
          SerialUSB.flush();
          mutex_exit(&core1_counter_mutex);
      }
    
      updateGpsData(1000);
      gpsDebug();

      if(gps.location.isValid() && gps.location.age()<1000){
        GpsInvalidTime=0;
        #if defined(ARDUINO_ARCH_RP2040)
        setStatusLEDBlinkCount(LED_STATUS_GPS_FIX);
        #endif
      }else{
        GpsInvalidTime++;
        #if defined(ARDUINO_ARCH_RP2040)
        if (gps.time.isValid()) setStatusLEDBlinkCount(LED_STATUS_GPS_TIME);
        else setStatusLEDBlinkCount(LED_STATUS_NO_GPS);
        #endif
        if(GpsInvalidTime > GpsResetTime){
          GpsOFF; 
          ublox_high_alt_mode_enabled = false; //gps sleep mode resets high altitude mode.
          Watchdog.reset();
          delay(1000);
          GpsON;
          GpsInvalidTime=0;     
        }
      }

      if ((gps.location.age() < 1000 || gps.location.isUpdated()) && gps.location.isValid()) {
        if (gps.satellites.isValid() && gps.satellites.value() > 3) {
          GpsFirstFix = true;
          if(readBatt() < HighVolt){
             GpsOFF; 
             ublox_high_alt_mode_enabled = false; //gps sleep mode resets high altitude mode.
          }      
          GpsInvalidTime=0;

          // Checks if there is an HF (WSPR) TX window is soon, if not then send APRS beacon
          if (!((minute() % 10 == 3 || minute() % 10 == 7) &&  second()>50 && readBatt() > WsprBattMin && timeStatus() == timeSet)){
             updateTelemetry();
            //APRS frequency isn't the same for the whole world. (for pico balloon only)
            if (!radioSetup || TxCount == 200) {
              configureFreqbyLocation();
            }

            if(!arissModEnabled && autoPathSizeHighAlt && gps.altitude.feet()>3000){
              //force to use high altitude settings (WIDE2-n)
              APRS_setPathSize(1);
            } else {
              //use default settings  
              APRS_setPathSize(pathSize);
            }

            //in some countries Airborne APRS is not allowed. (for pico balloon only)
            if (isAirborneAPRSAllowed()) {
              #if defined(ARDUINO_ARCH_RP2040)
              setStatusLEDBlinkCount(LED_STATUS_TX_APRS);
              #endif
              sendLocation();             
              #if defined(ARDUINO_ARCH_RP2040)
              setStatusLEDBlinkCount(LED_STATUS_GPS_TIME);
              #endif

            }
            freeMem();
            SerialUSB.flush();

          }   

          // preparations for HF starts one minute before TX time at minute 3, 7, 13, 17, 23, 27, 33, 37, 43, 47, 53 or 57. No APRS TX during this period...
          #if defined(DEVMODE2)
          printf("timeStatus():%u minute():%u\n", timeStatus(), minute());
          #endif
          if (readBatt() > WsprBattMin && timeStatus() == timeSet && ((minute() % 10 == 3) || (minute() % 10 == 7)) ) { 
            #if defined(DEVMODE2)
            printf("start WSPR\n");
            #endif
            GridLocator(hf_loc, gps.location.lat(), gps.location.lng());
            sprintf(hf_message,"%s %s",hf_call,hf_loc);
            
            #if defined(DEVMODE)
            SerialUSB.println(F("Digital HF Mode Prepearing"));
            SerialUSB.print(F("Grid Locator: "));
            SerialUSB.println(hf_loc);
            #endif
            
            //HF transmission starts at minute 4, 8, 14, 18, 24, 28, 34, 38, 44, 48, 54 or 58 
            while (((minute() % 10 != 4) || (minute() % 10 != 8)) && second() != 0) {
              Watchdog.reset();
              delay(1);
              #if defined(ARDUINO_ARCH_RP2040)
              updateStatusLED();
              #endif
            }
            #if defined(DEVMODE)
            SerialUSB.println(F("Digital HF Mode Sending..."));
            #endif          
            #if defined(ARDUINO_ARCH_RP2040)
            setStatusLEDBlinkCount(LED_STATUS_TX_WSPR);
            #endif
            encode();
            //HFSent=true;

            #if defined(DEVMODE)
            SerialUSB.println(F("Digital HF Mode Sent"));
            #endif             
            #if defined(ARDUINO_ARCH_RP2040)
            setStatusLEDBlinkCount(LED_STATUS_GPS_TIME);
            #endif
  
          } else {
            sleepSeconds(BeaconWait);
          }   
        }else {
          #if defined(DEVMODE)
          SerialUSB.println(F("Not enough sattelites"));
          #endif
        }
      }

    
  } else {
    sleepSeconds(BattWait);
  }
}

void sleepSeconds(int sec) {
  PttOFF;
  Si4463OFF;
  Si5351OFF;
  TcxoOFF;
  SerialUSB.flush();
  for (int i = 0; i < sec; i++) {
    if (GpsFirstFix){//sleep gps after first fix
      if (readBatt() < HighVolt){
        GpsOFF;
        ublox_high_alt_mode_enabled = false;
      }
    }else{
      if (readBatt() < BattMin){
        GpsOFF;
        ublox_high_alt_mode_enabled = false;
      }
    } 
     
    
    Watchdog.reset();
#if defined(ARDUINO_ARCH_SAMD)
    delay(1000);
#elif defined(ARDUINO_ARCH_RP2040)
    uint32_t usec = time_us_32();
    while ((time_us_32() - usec) < 1000000) {
      updateStatusLED();
    }
#endif
    
  }
  Watchdog.reset();
}

boolean isAirborneAPRSAllowed() {

  float tempLat = gps.location.lat();
  float tempLong = gps.location.lng();

  GEOFENCE_position(tempLat, tempLong);

  boolean airborne = true;

  if (GEOFENCE_no_tx == 1) {
    airborne = false;
  }

  return airborne;
}

boolean inARISSGeoFence(float tempLat, float tempLong) {
  boolean ariss = false;
  //North Africa
  if(tempLat>0 && tempLat<32 && tempLong>0 && tempLong<32){ariss = true;}
  //North Pacific
  if(tempLat>28 && tempLat<50 && tempLong>-180 && tempLong<-130){ariss = true;}
  //North Atlantic
  if(tempLat>25 && tempLat<42 && tempLong>-60 && tempLong<-33){ariss = true;} 
 
  return ariss;
}

void configureFreqbyLocation() {

  float tempLat = gps.location.lat();
  float tempLong = gps.location.lng();


  if(beaconViaARISS && inARISSGeoFence(tempLat, tempLong)) {
    APRS_setPath1("ARISS", Wide1);
    APRS_setPath2("WIDE2", Wide2);
    APRS_setPathSize(2);
    GEOFENCE_APRS_frequency=145825000;
    arissModEnabled = true;
  } else {

    GEOFENCE_position(tempLat,tempLong);      
    arissModEnabled = false;
  }  
  radioSetup = true;
}


void updatePosition(int high_precision, char *dao) {
  // Convert and set latitude NMEA string Degree Minute Hundreths of minutes ddmm.hh[S,N].
  char latStr[10];
  RawDegrees rawDeg = gps.location.rawLat();
  uint32_t min_nnnnn;
  char lat_dao = 0;
  min_nnnnn = rawDeg.billionths * 0.006;
  if ( ((min_nnnnn / (high_precision ? 1 : 100)) % 10) >= 5 && min_nnnnn < (6000000 - ((high_precision ? 1 : 100)*5)) ) {
    // round up. Avoid overflow (59.999999 should never become 60.0 or more)
    min_nnnnn = min_nnnnn + (high_precision ? 1 : 100)*5;
  }
  sprintf(latStr, "%02u%02u.%02u%c", (unsigned int ) (rawDeg.deg % 100), (unsigned int ) ((min_nnnnn / 100000) % 100), (unsigned int ) ((min_nnnnn / 1000) % 100), rawDeg.negative ? 'S' : 'N');
  if (dao)
    dao[0] = (char) ((min_nnnnn % 1000) / 11) + 33;
  APRS_setLat(latStr);

  // Convert and set longitude NMEA string Degree Minute Hundreths of minutes ddmm.hh[E,W].
  char lonStr[10];
  rawDeg = gps.location.rawLng();
  min_nnnnn = rawDeg.billionths * 0.006;
  if ( ((min_nnnnn / (high_precision ? 1 : 100)) % 10) >= 5 && min_nnnnn < (6000000 - ((high_precision ? 1 : 100)*5)) ) {
    min_nnnnn = min_nnnnn + (high_precision ? 1 : 100)*5;
  }
  sprintf(lonStr, "%03u%02u.%02u%c", (unsigned int ) (rawDeg.deg % 1000), (unsigned int ) ((min_nnnnn / 100000) % 100), (unsigned int ) ((min_nnnnn / 1000) % 100), rawDeg.negative ? 'W' : 'E');
  if (dao) {
    dao[1] = (char) ((min_nnnnn % 1000) / 11) + 33;
    dao[2] = 0;
  }

  APRS_setLon(lonStr);
  APRS_setTimeStamp(gps.time.hour(), gps.time.minute(),gps.time.second());
}


void updateTelemetry() {
  sprintf(telemetry_buff, "%03d", gps.course.isValid() ? (int)gps.course.deg() : 0);
  telemetry_buff[3] = '/';
  sprintf(telemetry_buff + 4, "%03d", gps.speed.isValid() ? (int)gps.speed.knots() : 0);
  telemetry_buff[7] = '/';
  telemetry_buff[8] = 'A';
  telemetry_buff[9] = '=';
  //sprintf(telemetry_buff + 10, "%06lu", (long)gps.altitude.feet());

  //fixing negative altitude values causing display bug on aprs.fi
  float tempAltitude = gps.altitude.feet();

  if (tempAltitude>0){
    //for positive values
    sprintf(telemetry_buff + 10, "%06lu", (long)tempAltitude);
  } else{
    //for negative values
    sprintf(telemetry_buff + 10, "%06d", (long)tempAltitude);
    } 
  
  telemetry_buff[16] = ' ';
  sprintf(telemetry_buff + 17, "%03d", TxCount);
  telemetry_buff[20] = 'T';
  telemetry_buff[21] = 'x';
  telemetry_buff[22] = 'C';
  Si5351ON;//little hack to prevent a BMP180 related issue 
  delay(1);
#if defined(ARDUINO_ARCH_SAMD)
  telemetry_buff[23] = ' '; float tempC = bmp.readTemperature();
#elif defined(ARDUINO_ARCH_RP2040)
  telemetry_buff[23] = ' '; float tempC = 0.f;
#endif
  dtostrf(tempC, 6, 2, telemetry_buff + 24);
  telemetry_buff[30] = 'C';
#if defined(ARDUINO_ARCH_SAMD)
  telemetry_buff[31] = ' '; float pressure = bmp.readPressure() / 100.0; //Pa to hPa
#elif defined(ARDUINO_ARCH_RP2040)
  telemetry_buff[31] = ' '; float pressure = 0.f; //Pa to hPa
#endif
  dtostrf(pressure, 7, 2, telemetry_buff + 32);
  Si5351OFF; 
  telemetry_buff[39] = 'h';
  telemetry_buff[40] = 'P';
  telemetry_buff[41] = 'a';
  telemetry_buff[42] = ' ';
  dtostrf(readBatt(), 5, 2, telemetry_buff + 43);
  telemetry_buff[48] = 'V';
  telemetry_buff[49] = ' ';
  sprintf(telemetry_buff + 50, "%02d", gps.satellites.isValid() ? (int)gps.satellites.value() : 0);
  telemetry_buff[52] = 'S';
  telemetry_buff[53] = ' ';

  mutex_enter_blocking(&commentMutex);
  sprintf(telemetry_buff + 54, "%s", comment);
  mutex_exit(&commentMutex);

  // APRS PRECISION AND DATUM OPTION http://www.aprs.org/aprs12/datum.txt ; this extension should be added at end of beacon message.
  // We only send this detailed info if it's likely we're interested in, i.e. searching for landing position
  if (send_aprs_enhanced_precision && gps.altitude.feet() < 10000L && strlen(telemetry_buff) < sizeof(telemetry_buff) - 1 - 5 - 1) /* room for " !wAB!\0" */ {
    char dao[3];
    updatePosition(1, dao);
    sprintf(telemetry_buff + strlen(telemetry_buff), " !w%s!", dao);
  } else {
    updatePosition(0, NULL);
  }

#if defined(DEVMODE)
  SerialUSB.println(telemetry_buff);
#endif

}

void sendLocation() {

#if defined(DEVMODE)
  SerialUSB.println(F("Location sending with comment"));
#endif

#if defined(ARDUINO_ARCH_SAMD)
  TcxoON;
  delay(10);
  Si4463ON;
  delay(20);            // wait for si4463 stable
  if (!si4463.init())
  {
    #if defined(DEVMODE)
    SerialUSB.println("Si4463 init fail!");  
    #endif  
    Si4463OFF;
    TcxoOFF;

  } else {
    #if defined(DEVMODE)
    SerialUSB.println("Si4463 Init OK");
    #endif  
    si4463.setFrequency(GEOFENCE_APRS_frequency+ APRS_Freq_Correction);
    si4463.setModemOOK();
    si4463.enterTxMode();
    analogWrite(A0, 128);
    PttON;
#elif defined(ARDUINO_ARCH_RP2040)
  vfo_set_drive_strength(APRS_TX_CLK_NUM, SI5351A_CLK_IDRV_8MA);
  vfo_turn_on(APRS_TX_CLK_NUM);
  vfo_set_freq_x16(APRS_TX_CLK_NUM, (GEOFENCE_APRS_frequency << PLL_CALCULATION_PRECISION));
  {
#endif
    delay(500);
    APRS_sendLoc(telemetry_buff);
    delay(10);
#if defined(ARDUINO_ARCH_SAMD)
    PttOFF;
    si4463.enterStandbyMode();
    Si4463OFF;
    TcxoOFF;
#elif defined(ARDUINO_ARCH_RP2040)
    vfo_turn_off();
#endif
    SerialUSB.print(F("APRS Location sent (Freq: "));
    SerialUSB.print(GEOFENCE_APRS_frequency);
    SerialUSB.print(F(") - "));    
    SerialUSB.println(TxCount);
    TxCount++;

  }

}

void sendStatus() {
#if defined(ARDUINO_ARCH_SAMD)
  TcxoON;
  delay(10);
  Si4463ON;
  delay(20);            // wait for si4463 stable
  if (!si4463.init())
  {
    #if defined(DEVMODE)
    SerialUSB.println("Si4463 init fail!");  
    #endif  
    Si4463OFF;
    TcxoOFF;

  } else {
    #if defined(DEVMODE)
    SerialUSB.println("Si4463 Init OK");
    #endif  
    si4463.setFrequency(GEOFENCE_APRS_frequency+ APRS_Freq_Correction);
    si4463.setModemOOK();
    si4463.enterTxMode();
    analogWrite(A0, 128);
    PttON;
#elif defined(ARDUINO_ARCH_RP2040)
  vfo_set_drive_strength(APRS_TX_CLK_NUM, SI5351A_CLK_IDRV_8MA);
  vfo_turn_on(APRS_TX_CLK_NUM);
  vfo_set_freq_x16(APRS_TX_CLK_NUM, (GEOFENCE_APRS_frequency << PLL_CALCULATION_PRECISION));
  {
#endif
    delay(500);
    mutex_enter_blocking(&StatusMessageMutex);
    APRS_sendStatus(StatusMessage);
    mutex_exit(&StatusMessageMutex);
    delay(10);
#if defined(ARDUINO_ARCH_SAMD)
    PttOFF;
    si4463.enterStandbyMode();
    Si4463OFF;
    TcxoOFF;
#elif defined(ARDUINO_ARCH_RP2040)
    vfo_turn_off();
#endif
    SerialUSB.print(F("Status sent (Freq: "));
    SerialUSB.print(GEOFENCE_APRS_frequency);
    SerialUSB.print(F(") - "));
    SerialUSB.println(TxCount);
    TxCount++;
  }

}

static void updateGpsData(int ms)
{
  Watchdog.reset();
  GpsON;
#if defined(ARDUINO_ARCH_SAMD)
  while (!Serial) {delay(1);} // wait for serial port to connect.  
  if(!ublox_high_alt_mode_enabled){
    //enable ublox high altitude mode
    setGPS_DynamicModel6();
    #if defined(DEVMODE)
      SerialUSB.println(F("ublox DynamicModel6 enabled..."));
    #endif      
    ublox_high_alt_mode_enabled = true;      
  }
#endif
  
  unsigned long start = millis();
  unsigned long bekle=0;
  do
  {
#if defined(ARDUINO_ARCH_SAMD)
    while (Serial.available()>0) {
      char c;
      c=Serial.read();
#elif defined(ARDUINO_ARCH_RP2040)
    while (Serial2.available()>0) {
      char c;
      c=Serial2.read();
#endif
      gps.encode(c);
      bekle= millis();
    }
    
    if (bekle!=0 && bekle+10<millis())break;
#if defined(ARDUINO_ARCH_RP2040)
    updateStatusLED();
#endif
  } while (millis() - start < ms);

  #if defined(DEVMODE2)
  printf("gps.time.isValid():%u\n", gps.time.isValid());
  #endif
  if (gps.time.isValid())
  {
    setTime(gps.time.hour(), gps.time.minute(), gps.time.second(), NULL, NULL, NULL);     
    #if defined(DEVMODE2)
    printf("setTime(%02u:%02u:%02u)\n", gps.time.hour(), gps.time.minute(), gps.time.second());
    #endif
    #if defined(ARDUINO_ARCH_RP2040)
    setStatusLEDBlinkCount(LED_STATUS_GPS_TIME);
    #endif
  }
}

//following GPS code from : https://github.com/HABduino/HABduino/blob/master/Software/habduino_v4/habduino_v4.ino
void setGPS_DynamicModel6()
{  
  int gps_set_sucess=0;
  uint8_t setdm6[] = {
  0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x06,
  0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00,
  0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0xDC };

  while(!gps_set_sucess)
  {
    #if defined(DEVMODE)
      SerialUSB.println(F("ublox DynamicModel6 try..."));
    #endif 
    sendUBX(setdm6, sizeof(setdm6)/sizeof(uint8_t));
    gps_set_sucess=getUBX_ACK(setdm6);
  }
}

void sendUBX(uint8_t *MSG, uint8_t len) {
#if defined(ARDUINO_ARCH_SAMD)
  Serial.write(0xFF);
  delay(500);
  for(int i=0; i<len; i++) {
    Serial.write(MSG[i]);
  }
#elif defined(ARDUINO_ARCH_RP2040)
  Serial2.write(0xFF);
  delay(500);
  for(int i=0; i<len; i++) {
    Serial2.write(MSG[i]);
  }
#endif
}

boolean getUBX_ACK(uint8_t *MSG) {
  uint8_t b;
  uint8_t ackByteID = 0;
  uint8_t ackPacket[10];
  unsigned long startTime = millis();
  boolean status =false;

  // Construct the expected ACK packet
  ackPacket[0] = 0xB5; // header
  ackPacket[1] = 0x62; // header
  ackPacket[2] = 0x05; // class
  ackPacket[3] = 0x01; // id
  ackPacket[4] = 0x02; // length
  ackPacket[5] = 0x00;
  ackPacket[6] = MSG[2]; // ACK class
  ackPacket[7] = MSG[3]; // ACK id
  ackPacket[8] = 0; // CK_A
  ackPacket[9] = 0; // CK_B

  // Calculate the checksums
  for (uint8_t ubxi=2; ubxi<8; ubxi++) {
    ackPacket[8] = ackPacket[8] + ackPacket[ubxi];
    ackPacket[9] = ackPacket[9] + ackPacket[8];
  }

  while (1) {
    // Test for success
    if (ackByteID > 9) {
      // All packets in order!
      status= true;
      break;
    }

    // Timeout if no valid response in 3 seconds
    if (millis() - startTime > 3000) {
      status= false;
      break;
    }

    // Make sure data is available to read
#if defined(ARDUINO_ARCH_SAMD)
    if (Serial.available()) {
      b = Serial.read();
#elif defined(ARDUINO_ARCH_RP2040)
    if (Serial2.available()) {
      b = Serial2.read();
#endif

      // Check that bytes arrive in sequence as per expected ACK packet
      if (b == ackPacket[ackByteID]) {
        ackByteID++;
      }
      else {
        ackByteID = 0; // Reset and look again, invalid order
      }
    }
  }
  return status;
}

void gpsDebug() {
#if defined(DEVMODE)
  SerialUSB.println();
  SerialUSB.println(F("Sats HDOP Latitude   Longitude   Fix  Date       Time     Date Alt    Course Speed Card Chars Sentences Checksum"));
  SerialUSB.println(F("          (deg)      (deg)       Age                      Age  (m)    --- from GPS ----  RX    RX        Fail"));
  SerialUSB.println(F("-----------------------------------------------------------------------------------------------------------------"));

  printInt(gps.satellites.value(), gps.satellites.isValid(), 5);
  printInt(gps.hdop.value(), gps.hdop.isValid(), 5);
  printFloat(gps.location.lat(), gps.location.isValid(), 11, 6);
  printFloat(gps.location.lng(), gps.location.isValid(), 12, 6);
  printInt(gps.location.age(), gps.location.isValid(), 5);
  printDateTime(gps.date, gps.time);
  printFloat(gps.altitude.meters(), gps.altitude.isValid(), 7, 2);
  printFloat(gps.course.deg(), gps.course.isValid(), 7, 2);
  printFloat(gps.speed.kmph(), gps.speed.isValid(), 6, 2);
  printStr(gps.course.isValid() ? TinyGPSPlus::cardinal(gps.course.value()) : "*** ", 6);

  printInt(gps.charsProcessed(), true, 6);
  printInt(gps.sentencesWithFix(), true, 10);
  printInt(gps.failedChecksum(), true, 9);
  SerialUSB.println();

#endif
}

static void printFloat(float val, bool valid, int len, int prec)
{
#if defined(DEVMODE)
  if (!valid)
  {
    while (len-- > 1)
      SerialUSB.print('*');
    SerialUSB.print(' ');
  }
  else
  {
    SerialUSB.print(val, prec);
    int vi = abs((int)val);
    int flen = prec + (val < 0.0 ? 2 : 1); // . and -
    flen += vi >= 1000 ? 4 : vi >= 100 ? 3 : vi >= 10 ? 2 : 1;
    for (int i = flen; i < len; ++i)
      SerialUSB.print(' ');
  }
#endif
}

static void printInt(unsigned long val, bool valid, int len)
{
#if defined(DEVMODE)
  char sz[32] = "*****************";
  if (valid)
    sprintf(sz, "%ld", val);
  sz[len] = 0;
  for (int i = strlen(sz); i < len; ++i)
    sz[i] = ' ';
  if (len > 0)
    sz[len - 1] = ' ';
  SerialUSB.print(sz);
#endif
}

static void printDateTime(TinyGPSDate &d, TinyGPSTime &t)
{
#if defined(DEVMODE)
  if (!d.isValid())
  {
    SerialUSB.print(F("********** "));
  }
  else
  {
    char sz[32];
    sprintf(sz, "%02d/%02d/%02d ", d.month(), d.day(), d.year());
    SerialUSB.print(sz);
  }

  if (!t.isValid())
  {
    SerialUSB.print(F("******** "));
  }
  else
  {
    char sz[32];
    sprintf(sz, "%02d:%02d:%02d ", t.hour(), t.minute(), t.second());
    SerialUSB.print(sz);
  }

  printInt(d.age(), d.isValid(), 5);
#endif
}

static void printStr(const char *str, int len)
{
#if defined(DEVMODE)
  int slen = strlen(str);
  for (int i = 0; i < len; ++i)
    SerialUSB.print(i < slen ? str[i] : ' ');
#endif
}


float readBatt() {
#if defined(ARDUINO_ARCH_SAMD)
  float R1 = 560000.0; // 560K
  float R2 = 100000.0; // 100K
  float value = 0.0f;

  do {    
    value =analogRead(BattPin);
    value +=analogRead(BattPin);
    value +=analogRead(BattPin);
    value = value / 3.0f;
    value = (value * 1.69) / 1024.0f;
    value = value / (R2/(R1+R2));
  } while (value > 20.0);
  return value ;
#elif defined(ARDUINO_ARCH_RP2040)
  return 5.0f;
#endif
}

#if defined(ARDUINO_ARCH_SAMD)
void TC3_Handler() {
  Adafruit_ZeroTimer::timerHandler(3);
}

void TimerCallback0(void)
{
 proceed = true;
}
#elif defined(ARDUINO_ARCH_RP2040)

#define WSPR_PWM_SLICE_NUM  4

static pwm_config wspr_pwm_config;

void PWM4_Handler(void) {
  pwm_clear_irq(WSPR_PWM_SLICE_NUM);
  static int cnt = 0;
  if (++cnt >= 500) {
    cnt = 0;
    proceed = true;
  }
}
#endif


void zeroTimerSetPeriodMs(float ms){
#if defined(ARDUINO_ARCH_SAMD)

  float freq = 1000/ms; 

  uint16_t divider  = 1;
  uint16_t compare = 0;
  tc_clock_prescaler prescaler = TC_CLOCK_PRESCALER_DIV1;

  if ((freq < 24000000) && (freq > 800)) {
    divider = 1;
    prescaler = TC_CLOCK_PRESCALER_DIV1;
    compare = 48000000/freq;
  } else if (freq > 400) {
    divider = 2;
    prescaler = TC_CLOCK_PRESCALER_DIV2;
    compare = (48000000/2)/freq;
  } else if (freq > 200) {
    divider = 4;
    prescaler = TC_CLOCK_PRESCALER_DIV4;
    compare = (48000000/4)/freq;
  } else if (freq > 100) {
    divider = 8;
    prescaler = TC_CLOCK_PRESCALER_DIV8;
    compare = (48000000/8)/freq;
  } else if (freq > 50) {
    divider = 16;
    prescaler = TC_CLOCK_PRESCALER_DIV16;
    compare = (48000000/16)/freq;
  } else if (freq > 12) {
    divider = 64;
    prescaler = TC_CLOCK_PRESCALER_DIV64;
    compare = (48000000/64)/freq;
  } else if (freq > 3) {
    divider = 256;
    prescaler = TC_CLOCK_PRESCALER_DIV256;
    compare = (48000000/256)/freq;
  } else if (freq >= 0.75) {
    divider = 1024;
    prescaler = TC_CLOCK_PRESCALER_DIV1024;
    compare = (48000000/1024)/freq;
  } 

  zerotimer.enable(false);
  zerotimer.configure(prescaler,       // prescaler
          TC_COUNTER_SIZE_16BIT,       // bit width of timer/counter
          TC_WAVE_GENERATION_MATCH_PWM // frequency or PWM mode
          );

  zerotimer.setCompare(0, compare);
  zerotimer.setCallback(true, TC_CALLBACK_CC_CHANNEL0, TimerCallback0);
  zerotimer.enable(true);
#elif defined(ARDUINO_ARCH_RP2040)
  wspr_pwm_config = pwm_get_default_config();
  pwm_config_set_clkdiv_int(&wspr_pwm_config, 250); // 2uS
  pwm_config_set_wrap(&wspr_pwm_config, ((uint16_t)ms - 1));
  pwm_init(WSPR_PWM_SLICE_NUM, &wspr_pwm_config, false);

  irq_set_exclusive_handler(PWM_IRQ_WRAP, PWM4_Handler);
  irq_set_enabled(PWM_IRQ_WRAP, true);
  pwm_clear_irq(WSPR_PWM_SLICE_NUM);
  pwm_set_irq_enabled(WSPR_PWM_SLICE_NUM, true);
  pwm_set_enabled(WSPR_PWM_SLICE_NUM, true);
#endif
}

void encode()
{
  Watchdog.reset();
#if defined(ARDUINO_ARCH_SAMD)
  analogWrite(A0, 0);
  TcxoON;
  delay(10);
  Si5351ON;
  delay(20);
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, HF_CORRECTION);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA); // Set for max power if desired
  si5351.output_enable(SI5351_CLK0, 0);
#elif defined(ARDUINO_ARCH_RP2040)
  vfo_set_drive_strength(WSPR_TX_CLK_NUM, SI5351A_CLK_IDRV_8MA);
  vfo_turn_on(WSPR_TX_CLK_NUM);
#endif
  uint8_t i;

  #if defined(DEVMODE2)
  printf("cur_mode:%u\n", cur_mode);
  #endif
  switch(cur_mode)
  {
  case MODE_JT9:
    hf_freq = JT9_DEFAULT_FREQ;
    symbol_count = JT9_SYMBOL_COUNT; // From the library defines
    tone_spacing = JT9_TONE_SPACING;
    tone_delay = JT9_DELAY;
    break;
  case MODE_JT65:
    hf_freq = JT65_DEFAULT_FREQ;
    symbol_count = JT65_SYMBOL_COUNT; // From the library defines
    tone_spacing = JT65_TONE_SPACING;
    tone_delay = JT65_DELAY;
    break;
  case MODE_JT4:
    hf_freq = JT4_DEFAULT_FREQ;
    symbol_count = JT4_SYMBOL_COUNT; // From the library defines
    tone_spacing = JT4_TONE_SPACING;
    tone_delay = JT4_DELAY;
    break;
  case MODE_WSPR:
#if defined(ARDUINO_ARCH_SAMD)
    hf_freq = WSPR_DEFAULT_FREQ;
#elif defined(ARDUINO_ARCH_RP2040)
    hf_freq = WSPR_DEFAULT_FREQ - 100 + (rand() % 200);
    #if defined(DEVMODE2)
    printf("WSPR freq: %u\n", hf_freq);
    #endif
#endif
    symbol_count = WSPR_SYMBOL_COUNT; // From the library defines
    tone_spacing = WSPR_TONE_SPACING;
    tone_delay = WSPR_DELAY;
    break;
  case MODE_FT8:
    hf_freq = FT8_DEFAULT_FREQ;
    symbol_count = FT8_SYMBOL_COUNT; // From the library defines
    tone_spacing = FT8_TONE_SPACING;
    tone_delay = FT8_DELAY;
    break;
  case MODE_FSQ_2:
    hf_freq = FSQ_DEFAULT_FREQ;
    tone_spacing = FSQ_TONE_SPACING;
    tone_delay = FSQ_2_DELAY;
    break;
  case MODE_FSQ_3:
    hf_freq = FSQ_DEFAULT_FREQ;
    tone_spacing = FSQ_TONE_SPACING;
    tone_delay = FSQ_3_DELAY;
    break;
  case MODE_FSQ_4_5:
    hf_freq = FSQ_DEFAULT_FREQ;
    tone_spacing = FSQ_TONE_SPACING;
    tone_delay = FSQ_4_5_DELAY;
    break;
  case MODE_FSQ_6:
    hf_freq = FSQ_DEFAULT_FREQ;
    tone_spacing = FSQ_TONE_SPACING;
    tone_delay = FSQ_6_DELAY;
    break;
  }
  set_tx_buffer();

  // Now transmit the channel symbols
  if(cur_mode == MODE_FSQ_2 || cur_mode == MODE_FSQ_3 || cur_mode == MODE_FSQ_4_5 || cur_mode == MODE_FSQ_6)
  {
    uint8_t j = 0;
    while(tx_buffer[j++] != 0xff);
    symbol_count = j - 1;
  }

#if defined(ARDUINO_ARCH_SAMD)
  // Reset the tone to the base frequency and turn on the output
  si5351.output_enable(SI5351_CLK0, 1);
#endif
  
  zeroTimerSetPeriodMs(tone_delay); 
  
  for(i = 0; i < symbol_count; i++)
  {
#if defined(ARDUINO_ARCH_SAMD)
      si5351.set_freq((hf_freq * 100) + (tx_buffer[i] * tone_spacing), SI5351_CLK0);
#elif defined(ARDUINO_ARCH_RP2040)
      uint32_t freq_x16 = (hf_freq << PLL_CALCULATION_PRECISION) + (tx_buffer[i] * (12000L << PLL_CALCULATION_PRECISION) + 4096) / 8192L;
      // printf("%s vfo_set_freq_x16(%u)\n", __func__, (freq_x16 >> PLL_CALCULATION_PRECISION));
      vfo_set_freq_x16(WSPR_TX_CLK_NUM, freq_x16);
#endif
      proceed = false;
#if defined(ARDUINO_ARCH_SAMD)
      while (!proceed);
#elif defined(ARDUINO_ARCH_RP2040)
      while (!proceed) {
        updateStatusLED();
      }
#endif
      Watchdog.reset();
  }
#if defined(ARDUINO_ARCH_SAMD)
  zerotimer.enable(false);
  // Turn off the output
  si5351.output_enable(SI5351_CLK0, 0);
  Si5351OFF;
  TcxoOFF;
#elif defined(ARDUINO_ARCH_RP2040)
  pwm_set_enabled(WSPR_PWM_SLICE_NUM, false);
  pwm_set_irq_enabled(WSPR_PWM_SLICE_NUM, false);
  pwm_clear_irq(WSPR_PWM_SLICE_NUM);
  irq_set_enabled(PWM_IRQ_WRAP, false);
  irq_remove_handler(PWM_IRQ_WRAP, PWM4_Handler);
  vfo_turn_off();
#endif
  Watchdog.reset();
}

void set_tx_buffer()
{
  // Clear out the transmit buffer
  memset(tx_buffer, 0, 255);

  // Set the proper frequency and timer CTC depending on mode
  switch(cur_mode)
  {
  case MODE_JT9:
    jtencode.jt9_encode(hf_message, tx_buffer);
    break;
  case MODE_JT65:
    jtencode.jt65_encode(hf_message, tx_buffer);
    break;
  case MODE_JT4:
    jtencode.jt4_encode(hf_message, tx_buffer);
    break;
  case MODE_WSPR:
    jtencode.wspr_encode(hf_call, hf_loc, dbm, tx_buffer);
    break;
  case MODE_FT8:
    jtencode.ft8_encode(hf_message, tx_buffer);
    break;
  case MODE_FSQ_2:
  case MODE_FSQ_3:
  case MODE_FSQ_4_5:
  case MODE_FSQ_6:
    jtencode.fsq_dir_encode(hf_call, "n0call", ' ', "hello world", tx_buffer);
    break;
  }
}

void GridLocator(char *dst, float latt, float lon) {
  int o1, o2;
  int a1, a2;
  float remainder;
  // longitude
  remainder = lon + 180.0;
  o1 = (int)(remainder / 20.0);
  remainder = remainder - (float)o1 * 20.0;
  o2 = (int)(remainder / 2.0);
  // latitude
  remainder = latt + 90.0;
  a1 = (int)(remainder / 10.0);
  remainder = remainder - (float)a1 * 10.0;
  a2 = (int)(remainder);

  dst[0] = (char)o1 + 'A';
  dst[1] = (char)a1 + 'A';
  dst[2] = (char)o2 + '0';
  dst[3] = (char)a2 + '0';
  dst[4] = (char)0;
}

void freeMem() {
#if defined(DEVMODE)
  SerialUSB.print(F("Free RAM: ")); SerialUSB.print(freeMemory(), DEC); SerialUSB.println(F(" byte"));
#endif

}

void setup1()
{
}


void loop1()
{
  mutex_enter_blocking(&core1_counter_mutex);
  core1_counter += 1;
  mutex_exit(&core1_counter_mutex);
  delay(1000);
}
