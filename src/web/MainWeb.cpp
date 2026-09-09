#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../PluginProcessor.h"
#include "WebServer.h"

namespace fs = std::filesystem;

static std::atomic<bool> g_shouldExit{false};

static void signalHandler(int signum) {
    std::cout << "\n[MainWeb] Received signal " << signum << ", shutting down gracefully..." << std::endl;
    g_shouldExit.store(true);
}

// Locate web directory containing index.html
static std::string findWebRoot(const std::string& customPath) {
    if (!customPath.empty() && fs::exists(customPath) && fs::exists(fs::path(customPath) / "index.html")) {
        return customPath;
    }

    std::vector<std::string> candidates = {
        "./web",
        "../web",
        "../../web",
        "/usr/local/share/autolevel-dj/web",
        "/usr/share/autolevel-dj/web"
    };

    for (const auto& p : candidates) {
        if (fs::exists(p) && fs::exists(fs::path(p) / "index.html")) {
            return fs::canonical(p).string();
        }
    }

    return "./web";
}

int main(int argc, char* argv[]) {
    std::cout << "========================================================" << std::endl;
    std::cout << "   AutoLevel DJ - Dedicated Hardware Web Server v1.0   " << std::endl;
    std::cout << "========================================================" << std::endl;

    // Parse command line arguments
    int port = 8080;
    std::string customWebDir = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--web" && i + 1 < argc) {
            customWebDir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: autolevel_web [options]\n"
                      << "  --port <number>   Port to listen on (default: 8080)\n"
                      << "  --web <dir>       Path to static web directory\n"
                      << "  --help            Show this help text\n";
            return 0;
        }
    }

    // Register signal handlers for clean exit
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialise JUCE
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    // Locate frontend files
    std::string webRoot = findWebRoot(customWebDir);
    std::cout << "[MainWeb] Serving web UI from: " << webRoot << std::endl;

    // Instantiate AutoLevel DJ Audio Processor
    AutoLevelDJAudioProcessor processor;

    // Create Audio Device Manager & Player
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer processorPlayer;
    processorPlayer.setProcessor(&processor);

    // Configure Audio Devices (Prioritize JACK on Linux, then ALSA FastTrack Pro, then default)
    std::cout << "[MainWeb] Initializing audio hardware..." << std::endl;
    
    bool audioReady = false;
    juce::String audioError;
    std::string deviceInfoStr = "Hardware Audio";

    // 1. Try JACK audio server first (standard for Patchbox OS / pro audio)
    const auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
    for (auto* type : deviceTypes) {
        if (type->getTypeName() == "JACK") {
            std::cout << "[MainWeb] Attempting to connect to JACK audio server..." << std::endl;
            deviceManager.setCurrentAudioDeviceType("JACK", true);
            audioError = deviceManager.initialise(2, 2, nullptr, true);
            if (audioError.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr) {
                audioReady = true;
                std::cout << "[MainWeb] Connected to JACK server successfully!" << std::endl;
                break;
            } else {
                std::cout << "[MainWeb] JACK connection not available: " << audioError.toStdString() << std::endl;
            }
        }
    }

    // 2. Fall back to ALSA and specifically search for Fast Track Pro
    if (!audioReady) {
        std::cout << "[MainWeb] Attempting ALSA hardware detection..." << std::endl;
        deviceManager.setCurrentAudioDeviceType("ALSA", true);

        juce::String preferredDevice = "";
        auto* alsaType = deviceManager.getCurrentDeviceTypeObject();
        if (alsaType != nullptr) {
            alsaType->scanForDevices();
            auto inputNames = alsaType->getDeviceNames(true);
            auto outputNames = alsaType->getDeviceNames(false);

            std::cout << "[MainWeb] Detected ALSA input devices:" << std::endl;
            for (const auto& name : inputNames) {
                std::cout << "   [IN]  " << name.toStdString() << std::endl;
                if (name.containsIgnoreCase("FastTrack") || name.containsIgnoreCase("Pro") || name.containsIgnoreCase("USB")) {
                    preferredDevice = name;
                }
            }

            std::cout << "[MainWeb] Detected ALSA output devices:" << std::endl;
            for (const auto& name : outputNames) {
                std::cout << "   [OUT] " << name.toStdString() << std::endl;
                if (preferredDevice.isEmpty() && (name.containsIgnoreCase("FastTrack") || name.containsIgnoreCase("Pro") || name.containsIgnoreCase("USB"))) {
                    preferredDevice = name;
                }
            }
        }

        if (preferredDevice.isNotEmpty()) {
            std::cout << "[MainWeb] Selecting detected USB interface: " << preferredDevice.toStdString() << std::endl;
        }

        audioError = deviceManager.initialise(2, 2, nullptr, true, preferredDevice);
        if (audioError.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr) {
            audioReady = true;
        }
    }

    // 3. Fall back to system defaults if specific search failed
    if (!audioReady) {
        std::cout << "[MainWeb] Fallback: Initializing with system default audio devices..." << std::endl;
        audioError = deviceManager.initialiseWithDefaultDevices(2, 2);
    }

    auto* currentDevice = deviceManager.getCurrentAudioDevice();
    if (currentDevice != nullptr) {
        std::string devName = currentDevice->getName().toStdString();
        int sampleRate = static_cast<int>(currentDevice->getCurrentSampleRate());
        int bufferSize = currentDevice->getCurrentBufferSizeSamples();
        auto inChans = currentDevice->getActiveInputChannels().countNumberOfSetBits();
        auto outChans = currentDevice->getActiveOutputChannels().countNumberOfSetBits();
        
        std::cout << "[MainWeb] Active Audio Device: " << devName 
                  << " (" << inChans << " Ins, " << outChans << " Outs, "
                  << sampleRate << " Hz, " << bufferSize << " samples)" << std::endl;
        
        deviceInfoStr = devName + " • " + std::to_string(sampleRate) + "Hz • " + std::to_string(bufferSize) + "smp";
    } else {
        std::cerr << "[MainWeb] Warning: No active audio device could be opened! " << audioError.toStdString() << std::endl;
    }

    // Connect processor to audio stream
    deviceManager.addAudioCallback(&processorPlayer);

    // Start Web Server
    autolevel::web::WebServer webServer(processor, webRoot);
    webServer.setAudioDeviceInfo(deviceInfoStr);

    if (!webServer.start(port)) {
        std::cerr << "[MainWeb] Fatal: Failed to start web server on port " << port << std::endl;
        deviceManager.removeAudioCallback(&processorPlayer);
        return 1;
    }

    std::cout << "\n========================================================" << std::endl;
    std::cout << " -> AutoLevel DJ Web Interface is ONLINE!" << std::endl;
    std::cout << " -> Access from your phone or laptop at:" << std::endl;
    std::cout << "    http://patchbox.local:" << port << std::endl;
    std::cout << "    http://localhost:" << port << std::endl;
    std::cout << "========================================================\n" << std::endl;

    // Main execution loop
    while (!g_shouldExit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::cout << "[MainWeb] Stopping audio and web services..." << std::endl;
    webServer.stop();
    deviceManager.removeAudioCallback(&processorPlayer);
    processorPlayer.setProcessor(nullptr);

    std::cout << "[MainWeb] Goodbye!" << std::endl;
    return 0;
}
