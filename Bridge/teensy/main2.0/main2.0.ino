// Teensy Audio Library Documentation: https://www.pjrc.com/teensy/gui/index.html

#include <Audio.h>
#include <Wire.h>
#include "Frequency.h"

// Global Variables
bool start_enable     = false;  // When true, start frequency is detected, and data flow can begin. Stop frequency detection will reset to false 
bool same_freq        = true;   // For checking that the same frequency is just being detected more than once
byte data_byte        = 0;      // 16 frequencies represent hexadecimal. 2 Hex character = 1 byte. Used for combining two bytes before sending to Pi
int iteration         = 0;      // Because the dominant frequency detected in one iteration of the FFT could be wrong,
                                // we are going to get a set of 10 of them and find the mode.
int mode_track[19];             // Each index will keep a count of how many times a freq is detected
                                // index 0 = no freq, index 1 = start freq, index 2 = stop freq, index 3 to 18 = 0x0 to 0xF;
int special_bit_track[2];       // Same idea for the special bit, determine if it's high or low for the majority of the 10 iteration
                                // index 0 = low, index 1 = high
                                
AudioInputI2S            audioInput;    // audio shield: mic  
AudioFilterStateVariable filter;        // High-pass filter
// TODO: Bandstop filter so that we could use both extreme ends of available frequencies 
AudioAnalyzeFFT1024      fft1024;

AudioConnection          patchCord1(audioInput, 0, filter, 0);
AudioConnection          patchCord2(filter, 2, fft1024, 0);

AudioControlSGTL5000     audioShield;     


int find_dominant_frequency_index() {
  // TODO: Selecting bins (the frequency index of the fft) with freq above 20 kHz.
  //       It seems unnecssary to use a high-pass filter earlier?
  float dominant_amp_value = 0;
  int dominant_freq_index = 0;
  
  for (int i = FREQ_0_INDEX; i < FREQ_SPECIAL_INDEX; ++i) {
    float amp_value = fft1024.read(i);
    // amp_value > 0.0001 for checking that frequency is at least picked up by microphone
    // TODO: play around with constraint, 0.0001 seems to be good for volume at 50%
    if (amp_value >= 0.0001 && amp_value > dominant_amp_value) {
      dominant_amp_value = amp_value;
      dominant_freq_index = i;
    }
  }

  return dominant_freq_index;
}


byte hz_to_binary(int freq_index) {
  switch (freq_index) {
    case FREQ_0_INDEX:
      return B00000000;
    case FREQ_1_INDEX:
      return B00000001;
    case FREQ_2_INDEX:
      return B00000010;
    case FREQ_3_INDEX:
      return B00000011;
    case FREQ_4_INDEX:
      return B00000100;
    case FREQ_5_INDEX:
      return B00000101;
    case FREQ_6_INDEX:
      return B00000110;
    case FREQ_7_INDEX:
      return B00000111;
    case FREQ_8_INDEX:
      return B00001000;
    case FREQ_9_INDEX:
      return B00001001;
    case FREQ_A_INDEX:
      return B00001010;
    case FREQ_B_INDEX:
      return B00001011;
    case FREQ_C_INDEX:
      return B00001100;
    case FREQ_D_INDEX:
      return B00001101;
    case FREQ_E_INDEX:
      return B00001110;
    case FREQ_F_INDEX:
      return B00001111;
    default:
      // TODO: it will enter here when the start bit is read
      return B00000000;
  }
}


void setup() {
  Serial1.begin(115200);
  
  // Audio connections require memory to work.
  AudioMemory(30);

  // Enable the audio shield and set the output volume.
  audioShield.enable();
  audioShield.inputSelect(AUDIO_INPUT_MIC);
  audioShield.volume(0.5);  // Max value is 0.8

  // Configure the window algorithm to use
  fft1024.windowFunction(AudioWindowHanning1024);
  //fft1024.windowFunction(NULL);
  // TODO: My understanding is that if exact multiple of 43 Hz, won't need window function.
  //       From what I notice though, windowFunction seems to better identify the frequency.

  // Filter out frequencies below 15 kHz
  // TODO: I don't know why over 15 kHz there starts to be issues with the FFT
  filter.frequency(15000);
}


void loop() {
  int data_freq_index = 0;
  int mode_freq_index = 0;
  bool special_bit_on = false;

  if (fft1024.available()) {
    // Compute the "main" frequency value
    data_freq_index = find_dominant_frequency_index();

    // Because the dominant frequency detected in one iteration of the FFT could be wrong,
    // we are going to get a set of 10 of them and find the mode.
    if (data_freq_index == 0) {
      ++mode_track[0];
    } else {
      ++mode_track[data_freq_index - (FREQ_0_INDEX - 1)];
    }

    // Same idea with the special bit
    if (fft1024.read(FREQ_SPECIAL_INDEX) > 0.0001) { // Play around with constraint, 0.0001 seems to be good for volume at 50%
      ++special_bit_track[1];
    } else {
      ++special_bit_track[0];
    }
    
    ++iteration;
    
    if (iteration == 10) {
      // Get the mode and reset all index to zero in the mode_track array
      int mode_count = mode_track[0];
      for (int i = 1; i < 19; ++i) {
        int count = mode_track[i];
        if (count > mode_count) {
          mode_freq_index = i + (FREQ_0_INDEX - 1);
          mode_count = count;
        }
        mode_track[i] = 0;
      }
      mode_track[0] = 0;
      iteration = 0;
      // Get whether the special bit is on or off
      special_bit_on = special_bit_track[1] > special_bit_track[0];
      special_bit_track[0] = 0;
      special_bit_track[1] = 0;
    } else {
      return; // next loop iteration
    }
    
    // Debugging Output
    Serial.print((mode_freq_index * 43) + 43);
    if (special_bit_on) {
      Serial.print("\t\t\tYes");
    }
    Serial.println();


    if (start_enable) {
      if (mode_freq_index == FREQ_STOP_INDEX) { // Stop data transmission
        start_enable = false;
        data_byte = 0;
          Serial1.write("Stop");  // TODO: Better start/stop serial
      } else if (FREQ_0_INDEX <= mode_freq_index && mode_freq_index <= FREQ_F_INDEX) { // TODO: Ideally, this else statement would always execute if the if statement doesn't
        byte half_byte = hz_to_binary(mode_freq_index);
        // TODO: handle when data_freq_index isn't within the expected range, 465 to 480 inclusive
        // TODO: big or little endian?
        // TODO: Problem with this method of detecting same_freq because what if special bit is detected on, but the data was suppose to be the same
        // If Special frequency is also detected, then the half_byte is the first half of the data_byte
        // If data frequency is the same one detected, then data_byte will just be overwritten with the same data, nothing changes
        if (special_bit_on) {
          data_byte = (half_byte << 4);
          same_freq = false;
          Serial.print(half_byte, BIN);
        }
        // If no special frequency detected, then the half_byte is the second half of the data_byte
        // But also check that the data frequency isn't the same frequency that was detected before
        else if (!same_freq) {
          data_byte = data_byte | half_byte;
          Serial.print(half_byte, BIN);
          Serial1.write(data_byte);
//          Serial.print((char)data_byte);
          data_byte = 0;
          same_freq = true;
        }
      }
    }
    else if (mode_freq_index == FREQ_START_INDEX) {
      start_enable = true;
        Serial1.write("Start");
    }
  }
}