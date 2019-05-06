/* 
	control_aic3206
	
	Created: Brendan Flynn (http://www.flexvoltbiosensor.com/) for Tympan, Jan-Feb 2017
	Purpose: Control module for Texas Instruments TLV320AIC3206 compatible with Teensy Audio Library
 
	License: MIT License.  Use at your own risk.
 */

#include "control_aic3206.h"


//********************************  Constants  *******************************//

#define AIC3206_I2C_ADDR                                             0x18


#ifndef AIC_FS
#  define AIC_FS                                                     44100UL
#endif

//define AIC_BITS                                                        16
#define AIC_BITS                                                        32

#define AIC_I2S_SLAVE                                                     1
#if AIC_I2S_SLAVE
// Direction of BCLK and WCLK (reg 27) is input if a slave:
# define AIC_CLK_DIR                                                    0
#else
// If master, make outputs:
# define AIC_CLK_DIR                                                   0x0C
#endif

//#ifndef AIC_CODEC_CLKIN_BCLK
//# define AIC_CODEC_CLKIN_BCLK                                           0
//#endif

//**************************** Clock Setup **********************************//

//**********************************  44100  *********************************//
#if AIC_FS == 44100

// MCLK = 180000000 * 16 / 255 = 11.294117 MHz // FROM TEENSY, FIXED

// PLL setup.  PLL_OUT = MCLK * R * J.D / P
//// J.D = 7.5264, P = 1, R = 1 => 90.32 MHz // FROM 12MHz CHA AND WHF //
// J.D = 7.9968, P = 1, R = 1 => 90.3168 MHz // For 44.1kHz exact
// J.D = 8.0000000002, P = 1, R = 1 => 9.35294117888MHz // for TEENSY 44.11764706kHz
#define PLL_J                                                             8
#define PLL_D                                                             0

// Bitclock divisor.
// BCLK = DAC_CLK/N = PLL_OUT/NDAC/N = 32*fs or 16*fs
// PLL_OUT = fs*NDAC*MDAC*DOSR
// BLCK = 32*fs = 1411200 = PLL
#if AIC_BITS == 16
#define BCLK_N                                                            8
#elif AIC_BITS == 32
#define BCLK_N                                                            4
#endif

// ADC/DAC FS setup.
// ADC_MOD_CLK = CODEC_CLKIN / (NADC * MADC)
// DAC_MOD_CLK = CODEC_CLKIN / (NDAC * MDAC)
// ADC_FS = PLL_OUT / (NADC*MADC*AOSR)
// DAC_FS = PLL_OUT / (NDAC*MDAC*DOSR)
// FS = 90.3168MHz / (8*2*128) = 44100 Hz.
// MOD = 90.3168MHz / (8*2) = 5644800 Hz

// Actual from Teensy: 44117.64706Hz * 128 => 5647058.82368Hz * 8*2 => 90352941.17888Hz

// DAC clock config.
// Note: MDAC*DOSR/32 >= RC, where RC is 8 for the default filter.
// See Table 2-21
// http://www.ti.com/lit/an/slaa463b/slaa463b.pdf
// PB1 - RC = 8.  Use M8, N2
// PB25 - RC = 12.  Use M8, N2

#define DOSR                                                            128
#define NDAC                                                              2
#define MDAC                                                              8

#define AOSR                                                            128
#define NADC                                                              2
#define MADC                                                              8

// Signal Processing Modes, Playback and Recording.
#define PRB_P                                                             1
#define PRB_R                                                             1

#endif // end fs if block

//**************************** Chip Setup **********************************//

//******************* INPUT DEFINITIONS *****************************//
// MIC routing registers
#define TYMPAN_MICPGA_LEFT_POSITIVE_REG  0x0134 // page 1 register 52
#define TYMPAN_MICPGA_LEFT_NEGATIVE_REG  0x0136 // page 1 register 54
#define TYMPAN_MICPGA_RIGHT_POSITIVE_REG 0x0137 // page 1 register 55
#define TYMPAN_MICPGA_RIGHT_NEGATIVE_REG 0x0139 // page 1 register 57

#define TYMPAN_MIC_ROUTING_POSITIVE_IN1     0b11000000 //
#define TYMPAN_MIC_ROUTING_POSITIVE_IN2     0b00110000 //
#define TYMPAN_MIC_ROUTING_POSITIVE_IN3     0b00001100 //
#define TYMPAN_MIC_ROUTING_POSITIVE_REVERSE 0b00000011 //

#define TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L  0b11000000 //
#define TYMPAN_MIC_ROUTING_NEGATIVE_IN2_REVERSE 0b00110000 //
#define TYMPAN_MIC_ROUTING_NEGATIVE_IN3_REVERSE 0b00001100 //
#define TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM2L  0b00000011 //

#define TYMPAN_MIC_ROUTING_RESISTANCE_10k 0b01010101
#define TYMPAN_MIC_ROUTING_RESISTANCE_20k 0b10101010
#define TYMPAN_MIC_ROUTING_RESISTANCE_40k 0b11111111
#define TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT TYMPAN_MIC_ROUTING_RESISTANCE_10k //datasheet (application notes) defaults to 20K...why?

#define TYMPAN_MICPGA_LEFT_VOLUME_REG    0x013B // page 1 register 59 // 0 to 47.5dB in 0.5dB steps
#define TYMPAN_MICPGA_RIGHT_VOLUME_REG   0x013C // page 1 register 60 // 0 to 47.5dB in 0.5dB steps

#define TYMPAN_MICPGA_VOLUME_ENABLE  0x00 // default is 0b11000000 - clear to 0 to enable

#define TYMPAN_MIC_BIAS_REG 0x0133 // page 1 reg 51
#define TYMPAN_MIC_BIAS_POWER_ON  0x40
#define TYMPAN_MIC_BIAS_POWER_OFF 0x00
#define TYMPAN_MIC_BIAS_OUTPUT_VOLTAGE_1_25     0x00
#define TYMPAN_MIC_BIAS_OUTPUT_VOLTAGE_1_7      0x01
#define TYMPAN_MIC_BIAS_OUTPUT_VOLTAGE_2_5      0x10
#define TYMPAN_MIC_BIAS_OUTPUT_VOLTAGE_VSUPPLY  0x11

#define TYMPAN_ADC_PROCESSING_BLOCK_REG 0x003d // page 0 register 61

#define TYMPAN_ADC_CHANNEL_POWER_REG 0x0051 // page 0 register81
#define TYMPAN_ADC_CHANNELS_ON 0b11000000 // power up left and right

#define TYMPAN_ADC_MUTE_REG 0x0052 //  page 0, register 82
#define TYMPAN_ADC_UNMUTE 0x00

void AudioControlAIC3206::setI2Cbus(int i2cBusIndex)
{
  // Setup for Master mode, pins 18/19, external pullups, 400kHz, 200ms default timeout
  switch (i2cBusIndex) {
	case 0:
		myWire = &Wire; break;
	//case 1:
	//	myWire = &Wire1; break;  //defined in WireKinetis.h via Teensy's 
	case 2:
		myWire = &Wire2; break; //defined in WireKinetis.h via Teensy's Wire.h
	//case 3:
	//	myWire = &Wire3; break; //commented out in WireKinetis.h?? Why?
	default:
		myWire = &Wire; break;
  }
}
bool AudioControlAIC3206::enable(void) {
  delay(100);
  myWire->begin();
  delay(5);

  //hard reset the AIC
  //Serial.println("Hardware reset of AIC...");
  //define RESET_PIN  21
  #define RESET_PIN (resetPinAIC)
  pinMode(RESET_PIN,OUTPUT); 
  digitalWrite(RESET_PIN,HIGH);delay(50); //not reset
  digitalWrite(RESET_PIN,LOW);delay(50);  //reset
  digitalWrite(RESET_PIN,HIGH);delay(50);//not reset
	
  aic_reset(); delay(100);  //soft reset
  aic_init(); delay(100);
  aic_initADC(); delay(100);
  aic_initDAC(); delay(100);

  aic_readPage(0, 27); // check a specific register - a register read test

  if (debugToSerial) Serial.println("AIC3206 enable done");

  return true;

}

bool AudioControlAIC3206::disable(void) {
  return true;
}

//dummy function to keep compatible with Teensy Audio Library
bool AudioControlAIC3206::inputLevel(float volume) {
  return false;
}

bool AudioControlAIC3206::inputSelect(int n) {
  if (n == TYMPAN_INPUT_LINE_IN) {
    // USE LINE IN SOLDER PADS
    aic_writeAddress(TYMPAN_MICPGA_LEFT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN1 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_LEFT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_RIGHT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN1 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_RIGHT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    // BIAS OFF
    setMicBias(TYMPAN_MIC_BIAS_OFF);
	

    if (debugToSerial) Serial.println("Set Audio Input to Line In");
    return true;
  } else if (n == TYMPAN_INPUT_JACK_AS_MIC) {
    // mic-jack = IN3
    aic_writeAddress(TYMPAN_MICPGA_LEFT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN3 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_LEFT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_RIGHT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN3 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_RIGHT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    // BIAS on, using default
    setMicBias(TYMPAN_DEFAULT_MIC_BIAS);

    if (debugToSerial) Serial.println("Set Audio Input to JACK AS MIC, BIAS SET TO DEFAULT 2.5V");
    return true;
  } else if (n == TYMPAN_INPUT_JACK_AS_LINEIN) {
    // 1
    // mic-jack = IN3
    aic_writeAddress(TYMPAN_MICPGA_LEFT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN3 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_LEFT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_RIGHT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN3 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_RIGHT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    // BIAS Off
    setMicBias(TYMPAN_MIC_BIAS_OFF);

    if (debugToSerial) Serial.println("Set Audio Input to JACK AS LINEIN, BIAS OFF");
    return true;
  } else if (n == TYMPAN_INPUT_ON_BOARD_MIC) {
    // on-board = IN2
    aic_writeAddress(TYMPAN_MICPGA_LEFT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN2 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_LEFT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_RIGHT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN2 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    aic_writeAddress(TYMPAN_MICPGA_RIGHT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
    // BIAS Off
    setMicBias(TYMPAN_MIC_BIAS_OFF);
    if (debugToSerial) Serial.println("Set Audio Input to Tympan On-Board MIC, BIAS OFF");

    return true;
  }
  Serial.print("AudioControlAIC3206: ERROR: Unable to Select Input - Value not supported: ");
  Serial.println(n);
  return false;
}

bool AudioControlAIC3206::setMicBias(int n) {
  if (n == TYMPAN_MIC_BIAS_1_25) {
    aic_writeAddress(TYMPAN_MIC_BIAS_REG, TYMPAN_MIC_BIAS_POWER_ON | TYMPAN_MIC_BIAS_OUTPUT_VOLTAGE_1_25); // power up mic bias
    return true;
  } else if (n == TYMPAN_MIC_BIAS_1_7) {
    aic_writeAddress(TYMPAN_MIC_BIAS_REG, TYMPAN_MIC_BIAS_POWER_ON | TYMPAN_MIC_BIAS_OUTPUT_VOLTAGE_1_7); // power up mic bias
    return true;
  } else if (n == TYMPAN_MIC_BIAS_2_5) {
    aic_writeAddress(TYMPAN_MIC_BIAS_REG, TYMPAN_MIC_BIAS_POWER_ON | TYMPAN_MIC_BIAS_OUTPUT_VOLTAGE_2_5); // power up mic bias
    return true;
  } else if (n == TYMPAN_MIC_BIAS_VSUPPLY) {
    aic_writeAddress(TYMPAN_MIC_BIAS_REG, TYMPAN_MIC_BIAS_POWER_ON | TYMPAN_MIC_BIAS_OUTPUT_VOLTAGE_VSUPPLY); // power up mic bias
    return true;
  } else if (n == TYMPAN_MIC_BIAS_OFF) {
    aic_writeAddress(TYMPAN_MIC_BIAS_REG, TYMPAN_MIC_BIAS_POWER_OFF); // power up mic bias
    return true;
  }
  Serial.print("AudioControlAIC3206: ERROR: Unable to set MIC BIAS - Value not supported: ");
  Serial.println(n);
  return false;
}

bool AudioControlAIC3206::enableDigitalMicInputs(bool desired_state) {
	if (desired_state == true) {
		//change the AIC's pin "MFP4" to clock input for digital microphone
		aic_writePage(0,55,0b00001110);  //page 0, register 55, bits D4-D1 to 0111
		
		//change the AIC's pin "MFP3" to Digital Microphone input
		aic_writePage(0,56,0b00000010);  //page 0, register 56, bits D2-D1 to 01
		
		//change the ADC to use the digital mic
		aic_writePage(0,81,0b11011100);  //page 0, register 81, Left+Right ADC powered, SCLK is Dig Mic In, Left+Right Dig Mic enabled, 1gain per word clock
		
		return true;
	} else {
		//change the AIC's pin "MFP4" to clock input for digital microphone
		aic_writePage(0,55,0b00000010);  //page 0, register 55, set to "disabled" ???  is this the default state?
		
		//change the IAC's pin "MFP3" to Digital Microphone input
		aic_writePage(0,56,0b00000010);  //page 0, register 56, set to "disabled" ???  is this the default state?

		//change the ADC to NOT use the digital mic
		aic_writePage(0,81,0b11000000);  //page 0, register 81, Left+Right ADC powered, GPIO as Dig Mic In, Left+Right Dig Mic disabled, gain per word clock
		
		return false;
	}
}

void AudioControlAIC3206::aic_reset() {
  if (debugToSerial) Serial.println("INFO: Reseting AIC");
  aic_writePage(0x00, 0x01, 0x01);
  // aic_writeAddress(0x0001, 0x01);

  delay(10);
}


// example - turn on IN3 - mic jack, with negatives routed to CM1L and with 10k resistance
// aic_writeAddress(TYMPAN_LEFT_MICPGA_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN3 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
// aic_writeAddress(TYMPAN_LEFT_MICPGA_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
// aic_writeAddress(TYMPAN_RIGHT_MICPGA_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN3 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
// aic_writeAddress(TYMPAN_RIGHT_MICPGA_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);

void AudioControlAIC3206::aic_initADC() {
  if (debugToSerial) Serial.println("INFO: Initializing AIC ADC");
  aic_writeAddress(TYMPAN_ADC_PROCESSING_BLOCK_REG, PRB_R);  // processing blocks - ADC
  aic_writePage(1, 61, 0); // 0x3D // Select ADC PTM_R4 Power Tune?  (this line is from datasheet (application guide, Section 4.2)
  aic_writePage(1, 71, 0b00110001); // 0x47 // Set MicPGA startup delay to 3.1ms
  aic_writeAddress(TYMPAN_MIC_BIAS_REG, TYMPAN_MIC_BIAS_POWER_ON | TYMPAN_MIC_BIAS_2_5); // power up mic bias

  aic_writeAddress(TYMPAN_MICPGA_LEFT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN2 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
  aic_writeAddress(TYMPAN_MICPGA_LEFT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
  aic_writeAddress(TYMPAN_MICPGA_RIGHT_POSITIVE_REG, TYMPAN_MIC_ROUTING_POSITIVE_IN2 & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);
  aic_writeAddress(TYMPAN_MICPGA_RIGHT_NEGATIVE_REG, TYMPAN_MIC_ROUTING_NEGATIVE_CM_TO_CM1L & TYMPAN_MIC_ROUTING_RESISTANCE_DEFAULT);

  aic_writeAddress(TYMPAN_MICPGA_LEFT_VOLUME_REG, TYMPAN_MICPGA_VOLUME_ENABLE); // enable Left MicPGA, set gain to 0 dB
  aic_writeAddress(TYMPAN_MICPGA_RIGHT_VOLUME_REG, TYMPAN_MICPGA_VOLUME_ENABLE); // enable Right MicPGA, set gain to 0 dB

  //aic_writePage(1, 58, 0b11111100); // Anti-thump on aill input channels...doesn't seem to od anything here.  :(
  
  aic_writeAddress(TYMPAN_ADC_MUTE_REG, TYMPAN_ADC_UNMUTE); // Unmute Left and Right ADC Digital Volume Control
  aic_writeAddress(TYMPAN_ADC_CHANNEL_POWER_REG, TYMPAN_ADC_CHANNELS_ON); // Unmute Left and Right ADC Digital Volume Control
}

// set MICPGA volume, 0-47.5dB in 0.5dB setps
bool AudioControlAIC3206::setInputGain_dB(float volume) {
  if (volume < 0.0) {
    volume = 0.0; // 0.0 dB
    Serial.println("AudioControlAIC3206: WARNING: Attempting to set MIC volume outside range");
  }
  if (volume > 47.5) {
    volume = 47.5; // 47.5 dB
    Serial.println("AudioControlAIC3206: WARNING: Attempting to set MIC volume outside range");
  }

  volume = volume * 2.0; // convert to value map (0.5 dB steps)
  int8_t volume_int = (int8_t) (round(volume)); // round

  if (debugToSerial) {
	Serial.print("INFO: Setting MIC volume to ");
	Serial.print(volume, 1);
	Serial.print(".  Converted to volume map => ");
	Serial.println(volume_int);
  }

  aic_writeAddress(TYMPAN_MICPGA_LEFT_VOLUME_REG, TYMPAN_MICPGA_VOLUME_ENABLE | volume_int); // enable Left MicPGA, set gain to 0 dB
  aic_writeAddress(TYMPAN_MICPGA_RIGHT_VOLUME_REG, TYMPAN_MICPGA_VOLUME_ENABLE | volume_int); // enable Right MicPGA, set gain to 0 dB
  return true;
}

//******************* OUTPUT DEFINITIONS *****************************//
#define TYMPAN_DAC_PROCESSING_BLOCK_REG 0x003c // page 0 register 60
#define TYMPAN_DAC_VOLUME_LEFT_REG 0x0041 // page 0 reg 65
#define TYMPAN_DAC_VOLUME_RIGHT_REG 0x0042 // page 0 reg 66


//volume control, similar to Teensy Audio Board
// value between 0.0 and 1.0.  Set to span -58 to +15 dB
bool AudioControlAIC3206::volume(float volume) {
	volume = max(0.0, min(1.0, volume));
	float vol_dB = -58.f + (15.0 - (-58.0f)) * volume;
	volume_dB(vol_dB);
	return true;
}

bool AudioControlAIC3206::enableAutoMuteDAC(bool enable, uint8_t mute_delay_code=7) {
	if (enable) {
		mute_delay_code = max(0,min(mute_delay_code,7));
		if (mute_delay_code == 0) enable = false;
	} else {
		mute_delay_code = 0;  //this disables the auto mute
	}
	uint8_t val = aic_readPage(0,64);
	val = val & 0b10001111;  //clear these bits
	val = val | (mute_delay_code << 4); //set these bits
	aic_writePage(0,64,val);
	return enable;
}

// -63.6 to +24 dB in 0.5dB steps.  uses signed 8-bit
bool AudioControlAIC3206::volume_dB(float volume) {

  // Constrain to limits
  if (volume > 24.0) {
    volume = 24.0;
    Serial.println("AudioControlAIC3206: WARNING: Attempting to set DAC Volume outside range");
  }
  if (volume < -63.5) {
    volume = -63.5;
    Serial.println("AudioControlAIC3206: WARNING: Attempting to set DAC Volume outside range");
  }

  volume = volume * 2.0; // convert to value map (0.5 dB steps)
  int8_t volume_int = (int8_t) (round(volume)); // round

  if (debugToSerial) {
	Serial.print("AudioControlAIC3206: Setting DAC volume to ");
	Serial.print(volume, 1);
	Serial.print(".  Converted to volume map => ");
	Serial.println(volume_int);
  }

  aic_writeAddress(TYMPAN_DAC_VOLUME_RIGHT_REG, volume_int);
  aic_writeAddress(TYMPAN_DAC_VOLUME_LEFT_REG, volume_int);
  return true;
}

void AudioControlAIC3206::aic_initDAC() {
	if (debugToSerial) Serial.println("AudioControlAIC3206: Initializing AIC DAC");
	outputSelect(TYMPAN_OUTPUT_HEADPHONE_JACK_OUT); //default
}

bool AudioControlAIC3206::outputSelect(int n) {
	// PLAYBACK SETUP: 
	//	HPL/HPR are headphone output left and right
	//	LOL/LOR are line output left and right
	
	aic_writeAddress(TYMPAN_DAC_PROCESSING_BLOCK_REG, PRB_P); // processing blocks - DAC

	//mute, disable, then power-down everything
	aic_writePage(1, 16, 0b01000000); // mute HPL Driver, 0 gain
	aic_writePage(1, 17, 0b01000000); // mute HPR Driver, 0 gain
	aic_writePage(1, 18, 0b01000000); // mute LOL Driver, 0 gain
	aic_writePage(1, 19, 0b01000000); // mute LOR Driver, 0 gain
	aic_writePage(0, 63, 0); //disable LDAC/RDAC
	aic_writePage(1, 9, 0); // Power down HPL/HPR and LOL/LOR drivers
	
	aic_writePage(1,12,0); //unroute from HPL
	aic_writePage(1,13,0); //unroute from HPR
	aic_writePage(1,14,0); //unroute from LOL
	aic_writePage(1,15,0); //unroute from LOR	
	
	//set the pop reduction settings, Page 1 Register 20 "Headphone Driver Startup Control"
	aic_writePage(1, 20, 0b10100101);  //soft routing step is 200ms, 5.0 time constants, assume 6K resistance

	if (n == TYMPAN_OUTPUT_HEADPHONE_JACK_OUT) {

		//aic_writePage(1, 20, 0x25); // 0x14 De-Pop
		//aic_writePage(1, 12, 8); // route LDAC/RDAC to HPL/HPR
		//aic_writePage(1, 13, 8); // route LDAC/RDAC to HPL/HPR
		aic_writePage(1, 12, 0b00001000); // route LDAC/RDAC to HPL/HPR
		aic_writePage(1, 13, 0b00001000); // route LDAC/RDAC to HPL/HPR
		aic_writePage(0, 63, 0xD6); // 0x3F // Power up LDAC/RDAC
		aic_writePage(1, 16, 0); // unmute HPL Driver, 0 gain
		aic_writePage(1, 17, 0); // unmute HPR Driver, 0 gain
		aic_writePage(1, 9, 0x30); // Power up HPL/HPR drivers  0b00110000
		delay(100);
		aic_writeAddress(TYMPAN_DAC_VOLUME_LEFT_REG,  0); // default to 0 dB
		aic_writeAddress(TYMPAN_DAC_VOLUME_RIGHT_REG, 0); // default to 0 dB
		aic_writePage(0, 64, 0); // 0x40 // Unmute LDAC/RDAC

		if (debugToSerial) Serial.println("AudioControlAIC3206: Set Audio Output to Headphone Jack");
		return true;
  } else if (n == TYMPAN_OUTPUT_LINE_OUT) {
    
		//aic_writePage(1, 20, 0x25); // 0x14 De-Pop
		aic_writePage(1, 14, 0b00001000); // route LDAC/RDAC to LOL/LOR
		aic_writePage(1, 15, 0b00001000); // route LDAC/RDAC to LOL/LOR
		aic_writePage(0, 63, 0xD6); // 0x3F // Power up LDAC/RDAC		
		aic_writePage(1, 18, 0); // unmute LOL Driver, 0 gain
		aic_writePage(1, 19, 0); // unmute LOR Driver, 0 gain
		aic_writePage(1, 9, 0b00001100); // Power up LOL/LOR drivers
		delay(100);
		aic_writeAddress(TYMPAN_DAC_VOLUME_LEFT_REG,  0); // default to 0 dB
		aic_writeAddress(TYMPAN_DAC_VOLUME_RIGHT_REG, 0); // default to 0 dB
		aic_writePage(0, 64, 0); // 0x40 // Unmute LDAC/RDAC

		if (debugToSerial) Serial.println("AudioControlAIC3206: Set Audio Output to Line Out");
		return true;
  }  else if (n == TYMPAN_OUTPUT_HEADPHONE_AND_LINE_OUT) {
	  	aic_writePage(1, 12, 0b00001000); // route LDAC/RDAC to HPL/HPR
		aic_writePage(1, 13, 0b00001000); // route LDAC/RDAC to HPL/HPR
		aic_writePage(1, 14, 0b00001000); // route LDAC/RDAC to LOL/LOR
		aic_writePage(1, 15, 0b00001000); // route LDAC/RDAC to LOL/LOR
		
		aic_writePage(0, 63, 0xD6); // 0x3F // Power up LDAC/RDAC
		aic_writePage(1, 18, 0); // unmute LOL Driver, 0 gain
		aic_writePage(1, 19, 0); // unmute LOR Driver, 0 gain		
		aic_writePage(1, 16, 0); // unmute HPL Driver, 0 gain
		aic_writePage(1, 17, 0); // unmute HPR Driver, 0 gain

		aic_writePage(1, 9, 0b00111100);       // Power up both the HPL/HPR and the LOL/LOR drivers  
		
		delay(100);
		aic_writeAddress(TYMPAN_DAC_VOLUME_LEFT_REG,  0); // default to 0 dB
		aic_writeAddress(TYMPAN_DAC_VOLUME_RIGHT_REG, 0); // default to 0 dB
		aic_writePage(0, 64, 0); // 0x40 // Unmute LDAC/RDAC

		if (debugToSerial) Serial.println("AudioControlAIC3206: Set Audio Output to Headphone Jack and Line out");
		return true;	
  }
  Serial.print("AudioControlAIC3206: ERROR: Unable to Select Output - Value not supported: ");
  Serial.println(n);
  return false;
}


void AudioControlAIC3206::aic_init() {
  if (debugToSerial) Serial.println("AudioControlAIC3206: Initializing AIC");
  
  // PLL
  aic_writePage(0, 4, 3); // 0x04 low PLL clock range, MCLK is PLL input, PLL_OUT is CODEC_CLKIN
  aic_writePage(0, 5, (PLL_J != 0 ? 0x91 : 0x11));
  aic_writePage(0, 6, PLL_J);
  aic_writePage(0, 7, PLL_D >> 8);
  aic_writePage(0, 8, PLL_D &0xFF);

  // CLOCKS
  aic_writePage(0, 11, 0x80 | NDAC); // 0x0B
  aic_writePage(0, 12, 0x80 | MDAC); // 0x0C
  aic_writePage(0, 13, 0); // 0x0D
  aic_writePage(0, 14, DOSR); // 0x0E
  // aic_writePage(0, 18, 0); // 0x12 // powered down, ADC_CLK same as DAC_CLK
  // aic_writePage(0, 19, 0); // 0x13 // powered down, ADC_MOD_CLK same as DAC_MOD_CLK
  aic_writePage(0, 18, 0x80 | NADC); // 0x12
  aic_writePage(0, 19, 0x80 | MADC); // 0x13
  aic_writePage(0, 20, AOSR);
  aic_writePage(0, 30, 0x80 | BCLK_N); // power up BLCK N Divider, default is 128

  // POWER
  aic_writePage(1, 0x01, 8); // Reg 1, Val = 8 = 0b00001000 = disable weak connection AVDD to DVDD.  Keep headphone charge pump disabled.
  aic_writePage(1, 0x02, 0); // Reg 2, Val = 0 = 0b00000000 = Enable Master Analog Power Control
  aic_writePage(1, 0x7B, 1); // Reg 123, Val = 1 = 0b00000001 = Set reference to power up in 40ms when analog blocks are powered up
  aic_writePage(1, 0x7C, 6); // Reg 124, Val = 6 = 0b00000110 = Charge Pump, full peak current (000), clock divider (110) to Div 6 = 333 kHz
  aic_writePage(1, 0x01, 10); // Reg 1, Val = 10 = 0x0A = 0b00001010.  Activate headphone charge pump.
  aic_writePage(1, 0x0A, 0); // Reg 10, Val = 0 = common mode 0.9 for full chip, HP, LO  // from WHF/CHA
  aic_writePage(1, 0x47, 0x31); // Reg 71, val = 0x31 = 0b00110001 = Set input power-up time to 3.1ms (for ADC)
  aic_writePage(1, 0x7D, 0x53); // Reg 125, Val = 0x53 = 0b01010011 = 0 10 1 00 11: HPL is master gain, Enable ground-centered mode, 100% output power, DC offset correction  // from WHF/CHA

  // !!!!!!!!! The below writes are from WHF/CHA - probably don't need?
  // aic_writePage(1, 1, 10); // 10 = 0b00001010 // weakly connect AVDD to DVDD.  Activate charge pump
 aic_writePage(0, 27, 0x01 | AIC_CLK_DIR | (AIC_BITS == 32 ? 0x30 : 0)); // 0x1B
  // aic_writePage(0, 28, 0); // 0x1C
}

unsigned int AudioControlAIC3206::aic_readPage(uint8_t page, uint8_t reg)
{
  unsigned int val;
  if (aic_goToPage(page)) {
    myWire->beginTransmission(AIC3206_I2C_ADDR);
    myWire->write(reg);
    unsigned int result = myWire->endTransmission();
    if (result != 0) {
      Serial.print("AudioControlAIC3206: ERROR: Read Page.  Page: ");Serial.print(page);
      Serial.print(" Reg: ");Serial.print(reg);
      Serial.print(".  Received Error During Read Page: ");
      Serial.println(result);
      val = 300 + result;
      return val;
    }
    if (myWire->requestFrom(AIC3206_I2C_ADDR, 1) < 1) {
      Serial.print("AudioControlAIC3206: ERROR: Read Page.  Page: ");Serial.print(page);
      Serial.print(" Reg: ");Serial.print(reg);
      Serial.println(".  Nothing to return");
      val = 400;
      return val;
    }
    if (myWire->available() >= 1) {
      uint16_t val = myWire->read();
	  if (debugToSerial) {
		Serial.print("AudioControlAIC3206: Read Page.  Page: ");Serial.print(page);
		Serial.print(" Reg: ");Serial.print(reg);
		Serial.print(".  Received: ");
		Serial.println(val, HEX);
	  }
      return val;
    }
  } else {
    Serial.print("AudioControlAIC3206: INFO: Read Page.  Page: ");Serial.print(page);
    Serial.print(" Reg: ");Serial.print(reg);
    Serial.println(".  Failed to go to read page.  Could not go there.");
    val = 500;
    return val;
  }
  val = 600;
  return val;
}

bool AudioControlAIC3206::aic_writeAddress(uint16_t address, uint8_t val) {
  uint8_t reg = (uint8_t) (address & 0xFF);
  uint8_t page = (uint8_t) ((address >> 8) & 0xFF);

  return aic_writePage(page, reg, val);
}

bool AudioControlAIC3206::aic_writePage(uint8_t page, uint8_t reg, uint8_t val) {
  if (debugToSerial) {
	Serial.print("AudioControlAIC3206: Write Page.  Page: ");Serial.print(page);
	Serial.print(" Reg: ");Serial.print(reg);
	Serial.print(" Val: ");Serial.println(val);
  }
  if (aic_goToPage(page)) {
    myWire->beginTransmission(AIC3206_I2C_ADDR);
    myWire->write(reg);delay(10);
    myWire->write(val);delay(10);
    uint8_t result = myWire->endTransmission();
    if (result == 0) return true;
    else {
      Serial.print("AudioControlAIC3206: Received Error During writePage(): Error = ");
      Serial.println(result);
    }
  }
  return false;
}

bool AudioControlAIC3206::aic_goToPage(byte page) {
  myWire->beginTransmission(AIC3206_I2C_ADDR);
  myWire->write(0x00); delay(10);// page register  //was delay(10) from BPF
  myWire->write(page); delay(10);// go to page   //was delay(10) from BPF
  byte result = myWire->endTransmission();
  if (result != 0) {
    Serial.print("AudioControlAIC3206: Received Error During goToPage(): Error = ");
    Serial.println(result);
    if (result == 2) {
      // failed to transmit address
      //return aic_goToPage(page);
    } else if (result == 3) {
      // failed to transmit data
      //return aic_goToPage(page);
    }
    return false;
  }
  return true;
}

bool AudioControlAIC3206::updateInputBasedOnMicDetect(int setting) {
	//read current mic detect setting
	int curMicDetVal = readMicDetect();
	if (curMicDetVal != prevMicDetVal) {
		if (curMicDetVal) {
			//enable the microphone input jack as our input
			inputSelect(setting);
		} else {
			//switch back to the on-board mics
			inputSelect(TYMPAN_INPUT_ON_BOARD_MIC);
		}
	}
	prevMicDetVal = curMicDetVal;
	return (bool)curMicDetVal;
}
bool AudioControlAIC3206::enableMicDetect(bool state) {
	//page 0, register 67
	byte curVal = aic_readPage(0,67);
	byte newVal = curVal;
	if (state) {
		//enable
		newVal = 0b111010111 & newVal;  //set bits 4-2 to be 010 to set debounce to 64 msec
		newVal = 0b10000000 | curVal;  //force bit 1 to 1 to enable headset to detect
		aic_writePage(0,67,newVal);  //bit 7 (=1) enable headset detect, bits 4-2 (=010) debounce to 64ms
	} else {
		//disable
		newVal = 0b01111111 & newVal;  //force bit 7 to zero to disable headset detect
		aic_writePage(0,67,newVal);  //bit 7 (=1) enable headset detect, bits 4-2 (=010) debounce to 64ms
	}
	return state;
}
int AudioControlAIC3206::readMicDetect(void) {
	//page 0, register 46, bit D4 (for D7-D0)
	byte curVal = aic_readPage(0,46);
	curVal = (curVal & 0b00010000);
	curVal = (curVal != 0);
	return curVal;
}

void computeFirstOrderHPCoeff_F32(float cutoff_Hz, float fs_Hz, float *coeff) {
	//cutoff_Hz is the cutoff frequency in Hz
	//fs_Hz is the sample rate in Hz
	
	//First-order Butterworth IIR
	//From https://www.dsprelated.com/showcode/199.php
	const float pi = 3.141592653589793;
	float T = 1.0f/fs_Hz; //sample period
	float w = cutoff_Hz * 2.0 * pi;
	float A = 1.0f / (tan( (w*T) / 2.0));
	coeff[0] = A / (1.0 + A); // first b coefficient
	coeff[1] = -coeff[0];     // second b coefficient
	coeff[2] = (1.0 - A) / (1.0 + A);  //second a coefficient (Matlab sign convention)
	coeff[2] = -coeff[2];  //flip to be TI sign convention
}
#define CONST_2_31_m1  (2147483647)   //2^31 - 1
void computeFirstOrderHPCoeff_i32(float cutoff_Hz, float fs_Hz, int32_t *coeff) {
	float coeff_f32[3];
	computeFirstOrderHPCoeff_F32(cutoff_Hz,fs_Hz,coeff_f32);
	for (int i=0; i<3; i++) {
		//scale
		coeff_f32[i] *= (float)CONST_2_31_m1;
		
		//truncate
		coeff[i] = (int32_t)coeff_f32[i];
	}
}
	
void AudioControlAIC3206::setHPFonADC(bool enable, float cutoff_Hz, float fs_Hz) { //fs_Hz is sample rate
	//see TI application guide Section 2.3.3.1.10.1: http://www.ti.com/lit/an/slaa463b/slaa463b.pdf
	uint32_t coeff[3];
	if (enable) {
		HP_cutoff_Hz = cutoff_Hz; 
		sample_rate_Hz = fs_Hz;
		computeFirstOrderHPCoeff_i32(cutoff_Hz,fs_Hz,(int32_t *)coeff);
		//Serial.print("enableHPFonADC: coefficients, Hex: ");
		//Serial.print(coeff[0],HEX);
		//Serial.print(", ");
		//Serial.print(coeff[1],HEX);
		//Serial.print(", ");
		//Serial.print(coeff[2],HEX);
		//Serial.println();
		
	} else {
		//disable
		HP_cutoff_Hz = cutoff_Hz;
		
		//see Table 5-4 in TI application guide  Coeff C4, C5, C6
		coeff[0] = 0x7FFFFFFF; coeff[1] = 0; coeff[2]=0;
	}
	
	setIIRCoeffOnADC(BOTH_CHAN, coeff); //needs twos-compliment
}


//set first-order IIR filter coefficients on ADC
void AudioControlAIC3206::setIIRCoeffOnADC(int chan, uint32_t *coeff) {

	//power down the AIC to allow change in coefficients
	uint32_t prev_state = aic_readPage(0x00,0x51);
	aic_writePage(0x00,0x51,prev_state & (0b00111111));  //clear first two bits
	
	if (chan == BOTH_CHAN) {
		setIIRCoeffOnADC_Left(coeff);
		setIIRCoeffOnADC_Right(coeff);
	} else if (chan == LEFT_CHAN) {
		setIIRCoeffOnADC_Left(coeff);
	} else {
		setIIRCoeffOnADC_Right(coeff);
	}

	//power the ADC back up
	aic_writePage(0x00,0x51,prev_state);  //clear first two bits
}
		
void AudioControlAIC3206::setIIRCoeffOnADC_Left(uint32_t *coeff) {
	int page;
	uint32_t c;
	
	//See TI AIC3206 Application Guide, Table 2-13: http://www.ti.com/lit/an/slaa463b/slaa463b.pdf
	
	//Coeff N0, Coeff C4
	page = 8;
	c = coeff[0];
	aic_writePage(page,24,(uint8_t)(c>>24));
	aic_writePage(page,25,(uint8_t)(c>>16));
	aic_writePage(page,26,(uint8_t)(c>>8));
	//int foo  = aic_readPage(page,24);	Serial.print("setIIRCoeffOnADC: first coefficient: ");  Serial.println(foo);

	//Coeff N1, Coeff C5
	c = coeff[1];
	aic_writePage(page,28,(uint8_t)(c>>24));
	aic_writePage(page,29,(uint8_t)(c>>16));
	aic_writePage(page,30,(uint8_t)(c>>8));
	
	//Coeff N2, Coeff C6
	c = coeff[2];
	aic_writePage(page,32,(uint8_t)(c>>24));
	aic_writePage(page,33,(uint8_t)(c>>16));
	aic_writePage(page,34,(uint8_t)(c>>9));	
}
void AudioControlAIC3206::setIIRCoeffOnADC_Right(uint32_t *coeff) {
	int page;
	uint32_t c;
	
	//See TI AIC3206 Application Guide, Table 2-13: http://www.ti.com/lit/an/slaa463b/slaa463b.pdf
				
	//Coeff N0, Coeff C36
	page = 9;
	c = coeff[0];
	aic_writePage(page,32,(uint8_t)(c>>24));
	aic_writePage(page,33,(uint8_t)(c>>16));
	aic_writePage(page,34,(uint8_t)(c>>8));

	//Coeff N1, Coeff C37
	c = coeff[1];
	aic_writePage(page,36,(uint8_t)(c>>24));
	aic_writePage(page,37,(uint8_t)(c>>16));
	aic_writePage(page,38,(uint8_t)(c>>8));
	
	//Coeff N2, Coeff C39
	c = coeff[2];;
	aic_writePage(page,40,(uint8_t)(c>>24));
	aic_writePage(page,41,(uint8_t)(c>>16));
	aic_writePage(page,42,(uint8_t)(c>>8));

}

bool AudioControlAIC3206::mixInput1toHPout(bool state) {
	int page = 1;
	int reg;
	uint8_t val;
	
	//loop over both channels
	for (reg = 12; reg <= 13; reg++) { //reg 12 is Left, reg 13 is right
		val = aic_readPage(page,reg);
		if (state == true) {  //activate
			val = val | 0b00000100; //set this bit.  Route IN1L to HPL
		} else {
			val = val & 0b11111011; //clear this bit.  Un-do routing of IN1L to HPL
		}
		aic_writePage(page,reg,val);
	}
	return state;
}