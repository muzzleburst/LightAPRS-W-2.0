#include <PicoBalloon.h>

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
  // Read sensor value...

  // Update comment and status based on sensor data...
  PicoBalloon.setComment("comment goes here");
  PicoBalloon.setStatus("status goes here");

  delay(1000);
}
