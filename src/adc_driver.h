#pragma once

#include <functional>
#include <wblib/wbmqtt.h>

#include <thread>

#include "config.h"


class TADCDriver
{
    public:

        TADCDriver(const WBMQTT::PDeviceDriver& mqttDriver, const TConfig& config);
        ~TADCDriver();

        void Stop();

    private:
        WBMQTT::PDeviceDriver  MqttDriver;
        WBMQTT::PLocalDevice   Device;

        bool                   Active;
        std::atomic_bool                    Cleaned;
        std::unique_ptr<std::thread>        Worker;
};