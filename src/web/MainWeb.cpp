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
            // Do NOT fall back to default device on failure here, so we can try ALSA cleanly
            audioError = deviceManager.initialise(2, 2, nullptr, false);
            if (audioError.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr) {
                audioReady = true;
                std::cout << "[MainWeb] Connected to JACK server successfully!" << std::endl;
                break;
            } else {
                std::cout << "[MainWeb] JACK connection not available: " << audioError.toStdString() << std::endl;
            }
        }
    }

    // 2. Fall back to ALSA and search for Fast Track Pro / USB audio hardware
    if (!audioReady) {
        std::cout << "[MainWeb] Attempting ALSA hardware detection..." << std::endl;
        deviceManager.setCurrentAudioDeviceType("ALSA", true);

        auto* alsaType = deviceManager.getCurrentDeviceTypeObject();
        if (alsaType != nullptr) {
            alsaType->scanForDevices();
            auto inputNames = alsaType->getDeviceNames(true);
            auto outputNames = alsaType->getDeviceNames(false);

            std::cout << "[MainWeb] Detected ALSA input devices (" << inputNames.size() << "):" << std::endl;
            for (int i = 0; i < inputNames.size(); ++i) {
                std::cout << "   [IN #" << i << "] " << inputNames[i].toStdString() << std::endl;
            }

            std::cout << "[MainWeb] Detected ALSA output devices (" << outputNames.size() << "):" << std::endl;
            for (int i = 0; i < outputNames.size(); ++i) {
                std::cout << "   [OUT #" << i << "] " << outputNames[i].toStdString() << std::endl;
            }

            auto scoreDevice = [](const juce::String& name, bool /*isInput*/) -> int {
                juce::String lower = name.toLowerCase();
                // Exclude broken kernel usbstream pseudo-device and S/PDIF digital output
                if (lower.contains("stream") || lower.contains("usbstream")) return -1000;
                if (lower.contains("iec958") || lower.contains("spdif") || lower.contains("s/pdif")) return -1000;

                int score = 0;
                if (lower.contains("fasttrack") || lower.contains("fast track")) score += 1000;
                else if (lower.contains("pro")) score += 500;
                else if (lower.contains("usb")) score += 200;
                else return -1000; // Require USB / FastTrack interface

                if (lower.contains("direct hardware") || lower.contains("hw:")) score += 100;
                else if (lower.contains("front")) score += 80;
                else if (lower.contains("direct sample")) score += 60; // dmix / dsnoop
                else if (lower.contains("default")) score += 40;

                return score;
            };

            std::vector<std::pair<int, juce::String>> inCandidates;
            for (const auto& name : inputNames) {
                int s = scoreDevice(name, true);
                if (s > 0) inCandidates.push_back({ s, name });
            }
            std::sort(inCandidates.begin(), inCandidates.end(), [](auto& a, auto& b) { return a.first > b.first; });

            std::vector<std::pair<int, juce::String>> outCandidates;
            for (const auto& name : outputNames) {
                int s = scoreDevice(name, false);
                if (s > 0) outCandidates.push_back({ s, name });
            }
            std::sort(outCandidates.begin(), outCandidates.end(), [](auto& a, auto& b) { return a.first > b.first; });

            std::cout << "[MainWeb] Found " << inCandidates.size() << " valid FastTrack inputs and "
                      << outCandidates.size() << " valid FastTrack outputs." << std::endl;

            for (const auto& inCand : inCandidates) {
                for (const auto& outCand : outCandidates) {
                    std::cout << "[MainWeb] Testing ALSA pair:\n   IN:  " << inCand.second.toStdString() 
                              << "\n   OUT: " << outCand.second.toStdString() << std::endl;

                    juce::AudioDeviceManager::AudioDeviceSetup setup;
                    deviceManager.getAudioDeviceSetup(setup);
                    setup.inputDeviceName = inCand.second;
                    setup.outputDeviceName = outCand.second;
                    setup.useDefaultInputChannels = false;
                    setup.useDefaultOutputChannels = false;
                    setup.inputChannels.clear();
                    setup.inputChannels.setBit(0);
                    setup.inputChannels.setBit(1);
                    setup.outputChannels.clear();
                    setup.outputChannels.setBit(0);
                    setup.outputChannels.setBit(1);
                    setup.sampleRate = 0; // Auto-negotiate hardware rate
                    setup.bufferSize = 0; // Auto-negotiate hardware buffer

                    audioError = deviceManager.setAudioDeviceSetup(setup, true);
                    if (audioError.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr) {
                        audioReady = true;
                        std::cout << "[MainWeb] Successfully opened Fast Track Pro ALSA audio device!" << std::endl;
                        break;
                    } else {
                        std::cout << "[MainWeb] Pair failed: " << audioError.toStdString() << std::endl;
                    }
                }
                if (audioReady) break;
            }
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
