#include <PicoBalloon.h>

void setup()
{
  PicoBalloon.begin("K6RGG", 11);
}

void loop()
{
  PicoBalloon.loop();
}

void setup1()
{
  // Configure sensor...
}

void loop1()
{
  // Read sensor value, then...
  PicoBalloon.setComment("comment goes here");
  PicoBalloon.setStatus("status goes here");

  delay(1000);
}
