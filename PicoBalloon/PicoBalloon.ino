#include <CPU.h>
#include <PicoBalloon.h>
#include <string>

// Create an instance of the CPU temperature sensor
CPU cpu;

// Core 0 runs radio module
void setup()
{
  PicoBalloon.begin("K6RGG", 11);
}

void loop()
{
  PicoBalloon.loop();
}

// Core 1 runs user application
void setup1()
{
  // Configure sensor...
}

void loop1()
{
  // Set comment.
  int temperature = cpu.getTemperature();
  PicoBalloon.setComment(std::string{"SF-HAB.org Pico Balloon CPU Temp: "} + std::to_string(temperature) + "C");

  delay(1000);
}
