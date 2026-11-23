#pragma once
class PauseIndicatorIface {
public:
    virtual ~PauseIndicatorIface() = default;
    virtual bool NeedToPauseNow() = 0;
};