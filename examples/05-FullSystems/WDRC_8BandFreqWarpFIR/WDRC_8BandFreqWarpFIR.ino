/*
   WDRC_8BandFreqWarpFIR

   Created: Chip Audette, Apr 2017
   Purpose: Multiband processing using frequency warped FIR from Kates, Chapter 8.
   Kates, James.  Digital Hearing Aids.  Plural Publishing Inc, San Diego, CA, USA.  2008.

   Uses Tympan.

   MIT License.  use at your own risk.
*/

//Include the needed Libraries and header files
#include <Tympan_Library.h>
#include "AudioFilterFreqWarpAllPassFIR_F32.h"
#include "SerialManager.h"


// Define the overall setup
String overall_name = String("Tympan: Frequency-Warping FIR Expander-Compressor-Limiter with Overall Limiter");
const int N_CHAN = 8;  //number of frequency bands (channels)
const float input_gain_dB = 15.0f; //gain on the microphone
float vol_knob_gain_dB = 0.0; //will be overridden by volume knob

int USE_VOLUME_KNOB = 1;  //set to 1 to use volume knob to override the default vol_knob_gain_dB set a few lines below

const float sample_rate_Hz = 24000.0f ; //24000 or 44117.64706f (or other frequencies in the table in AudioOutputI2S_F32)
const int audio_block_samples = 16;  //do not make bigger than AUDIO_BLOCK_SAMPLES from AudioStream.h (which is 128)
AudioSettings_F32   audio_settings(sample_rate_Hz, audio_block_samples);

//create audio library objects for handling the audio
Tympan       					audioHardware(TympanRev::D);   //TympanRev::D or TympanRev::C
AudioInputI2S_F32               i2s_in(audio_settings);        //Digital audio *from* the Teensy Audio Board ADC.
AudioTestSignalGenerator_F32    audioTestGenerator(audio_settings); //move this to be *after* the creation of the i2s_in object
AudioFilterFreqWarpAllPassFIR_F32   freqWarpFilterBank(audio_settings);
AudioEffectCompWDRC_F32        expCompLim[N_CHAN];     //here are the per-band compressors
AudioMixer8_F32                 mixer1;
AudioMixer8_F32                 mixer2;
AudioMixer8_F32                 mixer3;
AudioEffectCompWDRC_F32        compBroadband;     //here is the broadband compressor
AudioOutputI2S_F32              i2s_out(audio_settings);       //Digital audio *to* the Teensy Audio Board DAC.

//complete the creation of the tester objects
AudioTestSignalMeasurementMulti_F32  audioTestMeasurement_FIR(audio_settings);
AudioControlTestAmpSweep_F32    ampSweepTester(audio_settings,audioTestGenerator,audioTestMeasurement);
AudioControlTestFreqSweep_F32   freqSweepTester(audio_settings,audioTestGenerator,audioTestMeasurement);
AudioControlTestFreqSweep_F32   freqSweepTester_FIR(audio_settings,audioTestGenerator,audioTestMeasurement_FIR);


//make the audio connections
#define N_MAX_CONNECTIONS 100  //some large number greater than the number of connections that we'll make
AudioConnection_F32 *patchCord[N_MAX_CONNECTIONS];
int makeAudioConnections(void) { //call this in setup() or somewhere like that
  int count=0;

  //connect input
  patchCord[count++] = new AudioConnection_F32(i2s_in, 0, audioTestGenerator, 0); //#8 wants left, #3 wants right. //connect the Left input to the Left Int->Float converter

  //make the connection for the audio test measurements
  patchCord[count++] = new AudioConnection_F32(audioTestGenerator, 0, audioTestMeasurement, 0);
  patchCord[count++] = new AudioConnection_F32(audioTestGenerator, 0, audioTestMeasurement_FIR, 0);

  //connect to the filter bank
  patchCord[count++] = new AudioConnection_F32(audioTestGenerator, 0, freqWarpFilterBank, 0);

  //connect the filter bank to the compressors and then to the mixer
  for (int Ichan=0; Ichan < min(N_CHAN,8); Ichan++) {
    //patchCord[count++] = new AudioConnection_F32(freqWarpFilterBank, Ichan, mixer1, Ichan);
    patchCord[count++] = new AudioConnection_F32(freqWarpFilterBank, Ichan, expCompLim[Ichan], 0);
    patchCord[count++] = new AudioConnection_F32(expCompLim[Ichan], 0, mixer1, Ichan);
  }
  if (N_CHAN > 8) {
    for (int Ichan=0; Ichan < min(N_CHAN-8,8); Ichan++) {
      //patchCord[count++] = new AudioConnection_F32(freqWarpFilterBank, Ichan+8, mixer2, Ichan);
      patchCord[count++] = new AudioConnection_F32(freqWarpFilterBank, Ichan+8, expCompLim[Ichan+8], 0);
      patchCord[count++] = new AudioConnection_F32(expCompLim[Ichan+8], 0, mixer2, Ichan);
    }
  }

  patchCord[count++] = new AudioConnection_F32(mixer1, 0, mixer3, 0);
  patchCord[count++] = new AudioConnection_F32(mixer2, 0, mixer3, 1);

  for (int Ichan = 0; Ichan < N_CHAN; Ichan++) {
    patchCord[count++] = new AudioConnection_F32(freqWarpFilterBank, Ichan, audioTestMeasurement_FIR,1+Ichan);
  }

  patchCord[count++] = new AudioConnection_F32(mixer3, 0, compBroadband, 0);  //left output

  patchCord[count++] = new AudioConnection_F32(compBroadband, 0, i2s_out,0); //left output
  patchCord[count++] = new AudioConnection_F32(compBroadband, 0, i2s_out, 1);  //right output


  //make the connections for the audio test measurements
  patchCord[count++] = new AudioConnection_F32(audioTestGenerator, 0, audioTestMeasurement, 0);
  patchCord[count++] = new AudioConnection_F32(compBroadband, 0, audioTestMeasurement, 1);

  return count;
}

//control display and serial interaction
bool enable_printMemoryAndCPU = false;
void togglePrintMemroyAndCPU(void) { enable_printMemoryAndCPU = !enable_printMemoryAndCPU; }; //"extern" let's be it accessible outside
bool enable_printAveSignalLevels = false, printAveSignalLevels_as_dBSPL = false;
void togglePrintAveSignalLevels(bool as_dBSPL) { enable_printAveSignalLevels = !enable_printAveSignalLevels; printAveSignalLevels_as_dBSPL = as_dBSPL;};
SerialManager serialManager(N_CHAN,expCompLim,ampSweepTester,freqSweepTester,freqSweepTester_FIR);



//routine to setup the hardware
#define POT_PIN A1  //potentiometer is tied to this pin
void setupTympanHardware(void) {
  Serial.println("Setting up Tympan Audio Board...");
  audioHardware.enable(); // activate AIC

  //choose input
  switch (1) {
    case 1:
      //choose on-board mics
      audioHardware.inputSelect(TYMPAN_INPUT_ON_BOARD_MIC); // use the on board microphones
      break;
    case 2:
      //choose external input, as a line in
      audioHardware.inputSelect(TYMPAN_INPUT_JACK_AS_LINEIN); //
      break;
    case 3:
      //choose external mic plus the desired bias level
      audioHardware.inputSelect(TYMPAN_INPUT_JACK_AS_MIC); // use the microphone jack
      int myBiasLevel = TYMPAN_MIC_BIAS_2_5;  //choices: TYMPAN_MIC_BIAS_2_5, TYMPAN_MIC_BIAS_1_7, TYMPAN_MIC_BIAS_1_25, TYMPAN_MIC_BIAS_VSUPPLY
      audioHardware.setMicBias(myBiasLevel); // set mic bias to 2.5 // default
      break;
  }

  //set volumes
  audioHardware.volume_dB(0.f);  // -63.6 to +24 dB in 0.5dB steps.  uses signed 8-bit
  audioHardware.setInputGain_dB(input_gain_dB); // set MICPGA volume, 0-47.5dB in 0.5dB setps

  //setup pin for potentiometer
  pinMode(POT_PIN, INPUT); //set the potentiometer's input pin as an INPUT
}


// /////////// setup the audio processing
//define functions to setup the audio processing parameters
#include "GHA_Constants.h"  //this sets dsl and gha settings, which will be the defaults
#include "GHA_Alternates.h"  //this sets alternate dsl and gha, which can be switched in via commands
#define DSL_NORMAL 0
#define DSL_FULLON 1
int current_dsl_config = DSL_NORMAL; //used to select one of the configurations above for startup
float overall_cal_dBSPL_at0dBFS; //will be set later

//define the filterbank size
//#define N_FIR 96
#define N_FIR_FOO 32
//float firCoeff[N_CHAN][N_FIR];

void setupAudioProcessing(void) {

  //setup processing based on the DSL and GHA prescriptions
  if (current_dsl_config == DSL_NORMAL) {
    setupFromDSLandGHA(dsl, gha, N_CHAN, N_FIR_FOO, audio_settings);
  } else if (current_dsl_config == DSL_FULLON) {
    setupFromDSLandGHA(dsl_fullon, gha_fullon, N_CHAN, N_FIR_FOO, audio_settings);
  }
}

void setupFromDSLandGHA(const BTNRH_WDRC::CHA_DSL &this_dsl, const BTNRH_WDRC::CHA_WDRC &this_gha,
     const int n_chan_max, const int n_fir, const AudioSettings_F32 &settings)
{
  int n_chan = n_chan_max;  //maybe change this to be the value in the DSL itself.  other logic would need to change, too.

  //setup all of the per-channel compressors
  configurePerBandWDRCs(n_chan, settings.sample_rate_Hz, this_dsl, this_gha, expCompLim);

  //setup the broad band compressor (limiter)
  configureBroadbandWDRCs(settings.sample_rate_Hz, this_gha, vol_knob_gain_dB, compBroadband);

  //overwrite the one-point calibration based on the dsl data structure
  overall_cal_dBSPL_at0dBFS = this_dsl.maxdB;

}

void incrementDSLConfiguration(Stream *s) {
  current_dsl_config++;
  if (current_dsl_config==2) current_dsl_config=0;
  switch (current_dsl_config) {
    case (DSL_NORMAL):
      if (s) s->println("incrementDSLConfiguration: changing to NORMAL DSL configuration");
      setupFromDSLandGHA(dsl, gha, N_CHAN, N_FIR_FOO, audio_settings);  break;
    case (DSL_FULLON):
      if (s) s->println("incrementDSLConfiguration: changing to FULL-ON DSL configuration");
      setupFromDSLandGHA(dsl_fullon, gha_fullon, N_CHAN, N_FIR_FOO, audio_settings); break;
  }
}

void configureBroadbandWDRCs(float fs_Hz, const BTNRH_WDRC::CHA_WDRC &this_gha,
      float vol_knob_gain_dB, AudioEffectCompWDRC_F32 &WDRC)
{
  //assume all broadband compressors are the same
  //for (int i=0; i< ncompressors; i++) {
    //logic and values are extracted from from CHAPRO repo agc_prepare.c...the part setting CHA_DVAR

    //extract the parameters
    float atk = (float)this_gha.attack;  //milliseconds!
    float rel = (float)this_gha.release; //milliseconds!
    //float fs = this_gha.fs;
    float fs = (float)fs_Hz; // WEA override...not taken from gha
    float maxdB = (float) this_gha.maxdB;
    float exp_cr = (float) this_gha.exp_cr;
    float exp_end_knee = (float) this_gha.exp_end_knee;
    float tk = (float) this_gha.tk;
    float comp_ratio = (float) this_gha.cr;
    float tkgain = (float) this_gha.tkgain;
    float bolt = (float) this_gha.bolt;

    //set the compressor's parameters
    //WDRCs[i].setSampleRate_Hz(fs);
    //WDRCs[i].setParams(atk, rel, maxdB, exp_cr, exp_end_knee, tkgain, comp_ratio, tk, bolt);
    WDRC.setSampleRate_Hz(fs);
    WDRC.setParams(atk, rel, maxdB, exp_cr, exp_end_knee, tkgain + vol_knob_gain_dB, comp_ratio, tk, bolt);
 // }
}

void configurePerBandWDRCs(int nchan, float fs_Hz,
    const BTNRH_WDRC::CHA_DSL &this_dsl, const BTNRH_WDRC::CHA_WDRC &this_gha,
    AudioEffectCompWDRC_F32 *WDRCs)
{
  if (nchan > this_dsl.nchannel) {
    Serial.println(F("configureWDRC.configure: *** ERROR ***: nchan > dsl.nchannel"));
    Serial.print(F("    : nchan = ")); Serial.println(nchan);
    Serial.print(F("    : dsl.nchannel = ")); Serial.println(dsl.nchannel);
  }

  //now, loop over each channel
  for (int i=0; i < nchan; i++) {

    //logic and values are extracted from from CHAPRO repo agc_prepare.c
    float atk = (float)this_dsl.attack;   //milliseconds!
    float rel = (float)this_dsl.release;  //milliseconds!
    //float fs = gha->fs;
    float fs = (float)fs_Hz; // WEA override
    float maxdB = (float) this_dsl.maxdB;
    float exp_cr = (float)this_dsl.exp_cr[i];
    float exp_end_knee = (float)this_dsl.exp_end_knee[i];
    float tk = (float) this_dsl.tk[i];
    float comp_ratio = (float) this_dsl.cr[i];
    float tkgain = (float) this_dsl.tkgain[i];
    float bolt = (float) this_dsl.bolt[i];

    // adjust BOLT
    float cltk = (float)this_gha.tk;
    if (bolt > cltk) bolt = cltk;
    if (tkgain < 0) bolt = bolt + tkgain;

    //set the compressor's parameters
    WDRCs[i].setSampleRate_Hz(fs);
    WDRCs[i].setParams(atk,rel,maxdB,exp_cr,exp_end_knee,tkgain,comp_ratio,tk,bolt);
  }
}

// ///////////////// Main setup() and loop() as required for all Arduino programs

void setup() {
  Serial.begin(115200);   //Open USB Serial link...for debugging
  //if (USE_BT_SERIAL) BT_SERIAL.begin(115200);  //Open BT link
  delay(500);

  Serial.print(overall_name);Serial.println(": setup():...");
  Serial.print("Sample Rate (Hz): "); Serial.println(audio_settings.sample_rate_Hz);
  Serial.print("Audio Block Size (samples): "); Serial.println(audio_settings.audio_block_samples);
  //if (USE_BT_SERIAL) BT_SERIAL.print(overall_name);BT_SERIAL.println(": setup():...");

  // Audio connections require memory
  AudioMemory(20);      //allocate Int16 audio data blocks (need a few for under-the-hood stuff)
  AudioMemory_F32_wSettings(80,audio_settings);  //allocate Float32 audio data blocks (primary memory used for audio processing)

  //make all of the audio connections
  makeAudioConnections();

  // Enable the audio shield, select input, and enable output
  setupTympanHardware();

  //setup filters and mixers
  setupAudioProcessing();

  //End of setup
  if (USE_VOLUME_KNOB) servicePotentiometer(millis());
  //delay(200);
  //freqWarpFilterBank.setFirCoeff_viaFFT(); //prints filter coefficients
  //freqWarpFilterBank.setFirCoeff(); //prints filter coefficients
  printGainSettings();
  Serial.println("Setup complete.");

  //print help to tell user that we've started
  serialManager.printHelp();

} //end setup()

// define the loop() function, the function that is repeated over and over for the life of the device
void loop() {
  //choose to sleep ("wait for interrupt") instead of spinning our wheels doing nothing but consuming power
  asm(" WFI");  //ARM-specific.  Will wake on next interrupt.  The audio library issues tons of interrupts, so we wake up often.

  //respond to Serial commands
  while (Serial.available()) {
    serialManager.respondToByte((char)Serial.read());
  }

  //service the potentiometer...if enough time has passed
  if (USE_VOLUME_KNOB) servicePotentiometer(millis());

  //update the memory and CPU usage...if enough time has passed
  if (enable_printMemoryAndCPU) printMemoryAndCPU(millis());

  //print info about the signal processing
  updateAveSignalLevels(millis());
  if (enable_printAveSignalLevels) printAveSignalLevels(millis(),printAveSignalLevels_as_dBSPL);

} //end loop()


// ///////////////// Servicing routines

//servicePotentiometer: listens to the blue potentiometer and sends the new pot value
//  to the audio processing algorithm as a control parameter
void servicePotentiometer(unsigned long curTime_millis) {
  static unsigned long updatePeriod_millis = 100; //how many milliseconds between updating the potentiometer reading?
  static unsigned long lastUpdate_millis = 0;
  static float prev_val = -1.0;

  //has enough time passed to update everything?
  if (curTime_millis < lastUpdate_millis) lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - lastUpdate_millis) > updatePeriod_millis) { //is it time to update the user interface?

    //read potentiometer
    float val = float(analogRead(POT_PIN)) / 1024.0; //0.0 to 1.0
    val = (1.0/9.0) * (float)((int)(9.0 * val + 0.5)); //quantize so that it doesn't chatter...0 to 1.0

    //send the potentiometer value to your algorithm as a control parameter
    //float scaled_val = val / 3.0; scaled_val = scaled_val * scaled_val;
    if (abs(val - prev_val) > 0.05) { //is it different than befor?
      prev_val = val;  //save the value for comparison for the next time around

      setVolKnobGain_dB(val*45.0f - 10.0f - input_gain_dB);
    }
    lastUpdate_millis = curTime_millis;
  } // end if
} //end servicePotentiometer();


extern void printGainSettings(void) { //"extern" to make it available to other files, such as SerialManager.h
  Serial.print("Gain (dB): ");
  Serial.print("Vol Knob = "); Serial.print(vol_knob_gain_dB,1);
  Serial.print(", Input PGA = "); Serial.print(input_gain_dB,1);
  Serial.print(", Per-Channel = ");
  for (int i=0; i<N_CHAN; i++) {
    Serial.print(expCompLim[i].getGain_dB()-vol_knob_gain_dB,1);
    Serial.print(", ");
  }
  Serial.println();
}

extern void incrementKnobGain(float increment_dB) { //"extern" to make it available to other files, such as SerialManager.h
  setVolKnobGain_dB(vol_knob_gain_dB+increment_dB);
}

void setVolKnobGain_dB(float gain_dB) {
    float prev_vol_knob_gain_dB = vol_knob_gain_dB;
    vol_knob_gain_dB = gain_dB;
    float linear_gain_dB;
    for (int i=0; i<N_CHAN; i++) {
      linear_gain_dB = vol_knob_gain_dB + (expCompLim[i].getGain_dB()-prev_vol_knob_gain_dB);
      expCompLim[i].setGain_dB(linear_gain_dB);
    }
    printGainSettings();
}



void printMemoryAndCPU(unsigned long curTime_millis) {
  static unsigned long updatePeriod_millis = 3000; //how many milliseconds between updating gain reading?
  static unsigned long lastUpdate_millis = 0;

  //has enough time passed to update everything?
  if (curTime_millis < lastUpdate_millis) lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - lastUpdate_millis) > updatePeriod_millis) { //is it time to update the user interface?
    Serial.print("CPU Cur/Peak: ");
    Serial.print(audio_settings.processorUsage());
    //Serial.print(AudioProcessorUsage());
    Serial.print("%/");
    Serial.print(audio_settings.processorUsageMax());
    //Serial.print(AudioProcessorUsageMax());
    Serial.print("%,   ");
    Serial.print("Dyn MEM Int16 Cur/Peak: ");
    Serial.print(AudioMemoryUsage());
    Serial.print("/");
    Serial.print(AudioMemoryUsageMax());
    Serial.print(",   ");
    Serial.print("Dyn MEM Float32 Cur/Peak: ");
    Serial.print(AudioMemoryUsage_F32());
    Serial.print("/");
    Serial.print(AudioMemoryUsageMax_F32());
    Serial.println();

    lastUpdate_millis = curTime_millis; //we will use this value the next time around.
  }
}

float aveSignalLevels_dBFS[N_CHAN];
void updateAveSignalLevels(unsigned long curTime_millis) {
  static unsigned long updatePeriod_millis = 100; //how often to perform the averaging
  static unsigned long lastUpdate_millis = 0;
  float update_coeff = 0.2;

  //is it time to update the calculations
  if (curTime_millis < lastUpdate_millis) lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - lastUpdate_millis) > updatePeriod_millis) { //is it time to update the user interface?
    for (int i=0; i<N_CHAN; i++) { //loop over each band
      aveSignalLevels_dBFS[i] = (1.0-update_coeff)*aveSignalLevels_dBFS[i] + update_coeff*expCompLim[i].getCurrentLevel_dB(); //running average
    }
    lastUpdate_millis = curTime_millis; //we will use this value the next time around.
  }
}
void printAveSignalLevels(unsigned long curTime_millis, bool as_dBSPL) {
  static unsigned long updatePeriod_millis = 3000; //how often to print the levels to the screen
  static unsigned long lastUpdate_millis = 0;

  float offset_dB = 0.0f;
  String units_txt = String("dBFS");
  if (as_dBSPL) {
    offset_dB = overall_cal_dBSPL_at0dBFS;
    units_txt = String("dBSPL, approx");
  }

  //is it time to print to the screen
  if (curTime_millis < lastUpdate_millis) lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - lastUpdate_millis) > updatePeriod_millis) { //is it time to update the user interface?
    Serial.print("Ave Input Level (");Serial.print(units_txt); Serial.print("), Per-Band = ");
    for (int i=0; i<N_CHAN; i++) { Serial.print(aveSignalLevels_dBFS[i]+offset_dB,1);  Serial.print(", ");  }
    Serial.println();

    lastUpdate_millis = curTime_millis; //we will use this value the next time around.
  }
}
