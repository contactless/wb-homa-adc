#include <string>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cmath>
#include <ctime>
#include <unistd.h>

#include "sysfs_adc.h"
/*
namespace {
    struct ChannelName {
        int n;
        const char* name;
    };

    ChannelName channel_names[] = {
        {0, "adc0"},
        {1, "adc1"},
        {2, "adc2"},
        {3, "adc3"},
        {4, "adc4"},
        {5, "adc5"},
        {6, "adc6"},
        {7, "adc7"},
#if 0
        // later
        {0, "a1"},
        {0, "adc1"},
        {1, "a2"},
        {1, "adc2"},
        {2, "a3"},
        {2, "adc3"},
        {3, "a4"},
        {3, "adc4"},
        {4, "r1"},
        {5, "r4"},
        {6, "r2"},
        {7, "r3"},
#endif
        {-1, 0}
    };
    int GetChannelIndex(const std::string& name)
    {
        std::string locase_name = name;
        std::transform(locase_name.begin(), locase_name.end(), locase_name.begin(), ::tolower);
        for (ChannelName* name_item = channel_names; name_item->n >= 0; ++name_item) {
            if (locase_name == name_item->name)
                return name_item->n;
        }
        throw TAdcException("invalid channel name " + name);
    }

    int GetGPIOFromEnv(const std::string& name)
    {
        char* s = getenv(name.c_str());
        if (!s)
            throw TAdcException("Environment variable not set: " + name);
        try {
            return std::stoi(s);
        } catch (std::exception) {
            throw TAdcException("Invalid value of environment variable '" + name + "': " + s);
        }
    }
};
*/
TSysfsAdc::TSysfsAdc(const std::string& sysfs_dir, bool debug, const TChannel& channel_config)
    : SysfsDir(sysfs_dir),
    ChannelConfig(channel_config)
{
    AveragingWindow = ChannelConfig.AveragingWindow;
    Debug = debug;
    Initialized = false;
    string path_to_value = SysfsDir + "/bus/iio/devices/iio:device0/in_voltage" + to_string(ChannelConfig.ChannelNumber) + "_raw";
    AdcValStream.open(path_to_value);
    if (AdcValStream < 0) {
        throw TAdcException("error opening sysfs Adc file");
    }
    NumberOfChannels = channel_config.Mux.size();
}

TSysfsAdc::~TSysfsAdc(){
    if (AdcValStream.is_open()){
        AdcValStream.close();
    }
}

TSysfsAdcChannel TSysfsAdc::GetChannel(int i)
{
    // TBD: should pass chain_alias also (to be used instead of Name for the channel)
    return TSysfsAdcChannel(this, i, ChannelConfig.Mux[i].Id,ChannelConfig.Mux[i].Multiplier);
}

int TSysfsAdc::ReadValue(){
    int val;
    AdcValStream.seekg(0);
    AdcValStream >> val;
    return val;
}

TSysfsAdcMux::TSysfsAdcMux(const std::string& sysfs_dir, bool debug, const TChannel& channel_config)
    : TSysfsAdc(sysfs_dir, debug, channel_config)
{
    MinSwitchIntervalMs = ChannelConfig.MinSwitchIntervalMs;
    CurrentMuxInput = -1;
    GpioMuxA = ChannelConfig.Gpios[0];
    GpioMuxB = ChannelConfig.Gpios[1];
    GpioMuxC = ChannelConfig.Gpios[2];
   }


int TSysfsAdcMux::GetValue(int index)
{
    SetMuxABC(index);
    return ReadValue(); 
}

void TSysfsAdcMux::InitMux()
{
    if (Initialized)
        return;
    InitGPIO(GpioMuxA);
    InitGPIO(GpioMuxB);
    InitGPIO(GpioMuxC);
    Initialized = true;
}

void TSysfsAdcMux::InitGPIO(int gpio)
{
    std::string gpio_direction_path = GPIOPath(gpio, "/direction");
    std::ofstream setdirgpio(gpio_direction_path);
    if (!setdirgpio) {
        std::ofstream exportgpio(SysfsDir + "/class/gpio/export");
        if (!exportgpio)
            throw TAdcException("unable to export GPIO " + std::to_string(gpio));
        exportgpio << gpio << std::endl;
        setdirgpio.clear();
        setdirgpio.open(gpio_direction_path);
        if (!setdirgpio)
            throw TAdcException("unable to set GPIO direction");
    }
    setdirgpio << "out";
}

void TSysfsAdcMux::SetGPIOValue(int gpio, int value)
{
    std::ofstream setvalgpio(GPIOPath(gpio, "/value"));
    if (!setvalgpio)
        throw TAdcException("unable to set value of gpio " + std::to_string(gpio));
    setvalgpio << value << std::endl;
}

std::string TSysfsAdcMux::GPIOPath(int gpio, const std::string& suffix) const
{
    return std::string(SysfsDir + "/class/gpio/gpio") + std::to_string(gpio) + suffix;
}

void TSysfsAdcMux::MaybeWaitBeforeSwitching()
{
    if (MinSwitchIntervalMs <= 0)
        return;

    struct timespec tp;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp) < 0)
        throw TAdcException("unable to get timer value");

    if (CurrentMuxInput >= 0) { // no delays before the first switch
        double elapsed_ms = (tp.tv_sec - PrevSwitchTS.tv_sec) * 1000 +
            (tp.tv_nsec - PrevSwitchTS.tv_nsec) / 1000000;
        if (Debug)
            std::cerr << "elapsed: " << elapsed_ms << std::endl;
        if (elapsed_ms < MinSwitchIntervalMs) {
            if (Debug)
                std::cerr << "usleep: " << (MinSwitchIntervalMs - (int)elapsed_ms) * 1000 <<
                    std::endl;
            usleep((MinSwitchIntervalMs - (int)elapsed_ms) * 1000);
        }
    }
    PrevSwitchTS = tp;
}

void TSysfsAdcMux::SetMuxABC(int n)
{
    InitMux();
    if (CurrentMuxInput == n)
        return;
    if (Debug)
        std::cerr << "SetMuxABC: " << n << std::endl;
    SetGPIOValue(GpioMuxA, n & 1);
    SetGPIOValue(GpioMuxB, n & 2);
    SetGPIOValue(GpioMuxC, n & 4);
    CurrentMuxInput = n;
    usleep(MinSwitchIntervalMs * 1000);
}

TSysfsAdcPhys::TSysfsAdcPhys(const std::string& sysfs_dir, bool debug, const TChannel& channel_config)
    : TSysfsAdc(sysfs_dir, debug, channel_config)
{
}

int TSysfsAdcPhys::GetValue(int index)
{
   return ReadValue();
}


TSysfsAdcChannel::TSysfsAdcChannel(TSysfsAdc* owner, int index, const std::string& name, float multiplier)
    : d(new TSysfsAdcChannelPrivate())
{
    d->Owner.reset(owner);
    d->Index = index;
    d->Name = name;
    d->Buffer = new int[d->Owner->AveragingWindow](); // () initializes with zeros
    d->Multiplier = multiplier;
}

int TSysfsAdcChannel::GetValue()
{
    if (!d->Ready) {
        for (int i = 0; i < d->Owner->AveragingWindow; ++i) {
            int v = d->Owner->GetValue(d->Index);
            d->Buffer[i] = v;
            d->Sum += v;
        }
        d->Ready = true;
    } else {
        int v = d->Owner->GetValue(d->Index);
        d->Sum -= d->Buffer[d->Pos];
        d->Sum += v;
        d->Buffer[d->Pos++] = v;
        d->Pos %= d->Owner->AveragingWindow;
    }

    return round(d->Sum / d->Owner->AveragingWindow);
}

const std::string& TSysfsAdcChannel::GetName() const
{
    return d->Name;
}
