#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include "../PluginProcessor.h"

namespace autolevel::web {

class WebServer {
public:
    WebServer(AutoLevelDJAudioProcessor& processor, std::string webRoot);
    ~WebServer();

    bool start(int port = 8080);
    void stop();
    bool isRunning() const noexcept { return m_running.load(); }

    void setAudioDeviceInfo(const std::string& info);

private:
    std::string getStateJson();
    std::string getVisualStateJson();
    void setParameterValue(const std::string& paramId, float value);

    AutoLevelDJAudioProcessor& m_processor;
    std::string m_webRoot;
    std::string m_audioDeviceInfo = "JACK • 48kHz • 128smp";
    std::atomic<bool> m_running{false};
    std::thread m_serverThread;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace autolevel::web
