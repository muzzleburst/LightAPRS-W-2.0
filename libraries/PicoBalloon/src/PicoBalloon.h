#pragma once

#include <string>
#include <pico/mutex.h>


class PicoBalloon_ final
{
private:
    PicoBalloon_() = default;

public:
    static PicoBalloon_ &getInstance();

    PicoBalloon_(const PicoBalloon_&) = delete;
    PicoBalloon_ &operator=(const PicoBalloon_&) = delete;

public:
    void  begin(const std::string& callSign, uint8_t callNumber);
    void  loop();
    const std::string getComment() const;
    void  setComment(const std::string& message);
    const std::string getStatus() const;
    void  setStatus(const std::string& message);
};


extern PicoBalloon_ &PicoBalloon;
