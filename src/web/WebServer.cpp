#include "WebServer.h"
#include <httplib.h>
#include <sstream>
#include <iostream>
#include <chrono>
#include <regex>

namespace autolevel::web {

struct WebServer::Impl {
    httplib::Server svr;
};

WebServer::WebServer(AutoLevelDJAudioProcessor& processor, std::string webRoot)
    : m_processor(processor), m_webRoot(std::move(webRoot)), m_impl(std::make_unique<Impl>()) {
    
    // Serve static files from webRoot
    if (!m_webRoot.empty()) {
        m_impl->svr.set_mount_point("/", m_webRoot);
    }

    // CORS and cache control headers
    m_impl->svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        if (req.method == "OPTIONS") {
            res.status = 200;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // GET /api/state - Return current parameter configuration
    m_impl->svr.Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(getStateJson(), "application/json");
    });

    // POST /api/param - Update parameter value
    m_impl->svr.Post("/api/param", [this](const httplib::Request& req, httplib::Response& res) {
        std::regex idRegex("\"id\"\\s*:\\s*\"([^\"]+)\"");
        std::regex valRegex("\"value\"\\s*:\\s*([-+]?[0-9]*\\.?[0-9]+)");
        std::smatch idMatch, valMatch;

        if (std::regex_search(req.body, idMatch, idRegex) &&
            std::regex_search(req.body, valMatch, valRegex)) {
            std::string id = idMatch[1].str();
            float value = std::stof(valMatch[1].str());
            setParameterValue(id, value);
            res.set_content("{\"status\":\"ok\"}", "application/json");
        } else {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid JSON format\"}", "application/json");
        }
    });

    // POST /api/reset - Reset loudness integration history
    m_impl->svr.Post("/api/reset", [this](const httplib::Request&, httplib::Response& res) {
        m_processor.resetIntegration();
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // POST /api/bypass - Toggle master bypass
    m_impl->svr.Post("/api/bypass", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        auto* p = m_processor.getAPVTS().getRawParameterValue(AutoLevelDJAudioProcessor::ID_BYPASS);
        if (p != nullptr) {
            float current = p->load();
            float next = (current > 0.5f) ? 0.0f : 1.0f;
            setParameterValue(AutoLevelDJAudioProcessor::ID_BYPASS, next);
        }
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // GET /api/stream - Server-Sent Events (SSE) telemetry stream @ ~30 FPS
    m_impl->svr.Get("/api/stream", [this](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider(
            "text/event-stream",
            [this](size_t /*offset*/, httplib::DataSink& sink) {
                if (!m_running.load()) {
                    return false;
                }

                std::string json = getVisualStateJson();
                std::string sseMessage = "data: " + json + "\n\n";

                if (!sink.write(sseMessage.data(), sseMessage.size())) {
                    return false; // Client disconnected
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 fps
                return true;
            }
        );
    });
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start(int port) {
    if (m_running.load()) {
        return true;
    }

    m_running.store(true);
    m_serverThread = std::thread([this, port]() {
        std::cout << "[WebServer] Listening on http://0.0.0.0:" << port << std::endl;
        if (!m_impl->svr.listen("0.0.0.0", port)) {
            std::cerr << "[WebServer] Failed to bind to port " << port << std::endl;
            m_running.store(false);
        }
    });

    // Give server a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return m_running.load();
}

void WebServer::stop() {
    if (m_running.exchange(false)) {
        m_impl->svr.stop();
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }
        std::cout << "[WebServer] Stopped." << std::endl;
    }
}

void WebServer::setAudioDeviceInfo(const std::string& info) {
    m_audioDeviceInfo = info;
}

void WebServer::setParameterValue(const std::string& paramId, float value) {
    auto& apvts = m_processor.getAPVTS();
    auto* param = apvts.getParameter(paramId);
    if (param != nullptr) {
        auto range = apvts.getParameterRange(paramId);
        float normalized = range.convertTo0to1(value);
        param->setValueNotifyingHost(normalized);
    }
}

std::string WebServer::getStateJson() {
    auto& apvts = m_processor.getAPVTS();
    auto getVal = [&](const char* id, float defaultVal) -> float {
        auto* raw = apvts.getRawParameterValue(id);
        return raw ? raw->load() : defaultVal;
    };

    char buf[512];
    snprintf(buf, sizeof(buf),
        "{"
        "\"target_lufs\":%.1f,"
        "\"max_boost\":%.1f,"
        "\"max_cut\":%.1f,"
        "\"level_response\":%.2f,"
        "\"tone_slope\":%.1f,"
        "\"target_profile\":%d,"
        "\"mbc_speed\":%d,"
        "\"sub_weight\":%d,"
        "\"air_exciter\":%d,"
        "\"ceiling_db\":%.1f,"
        "\"hpf_freq\":%.1f,"
        "\"freeze_breakdowns\":%s,"
        "\"bypass\":%s"
        "}",
        getVal(AutoLevelDJAudioProcessor::ID_TARGET_LUFS, -14.0f),
        getVal(AutoLevelDJAudioProcessor::ID_MAX_BOOST, 6.0f),
        getVal(AutoLevelDJAudioProcessor::ID_MAX_CUT, -12.0f),
        getVal(AutoLevelDJAudioProcessor::ID_LEVEL_RESPONSE, 0.85f),
        getVal(AutoLevelDJAudioProcessor::ID_TONE_SLOPE, -1.5f),
        static_cast<int>(getVal(AutoLevelDJAudioProcessor::ID_TARGET_PROFILE, 1.0f)),
        static_cast<int>(getVal(AutoLevelDJAudioProcessor::ID_MBC_SPEED, 1.0f)),
        static_cast<int>(getVal(AutoLevelDJAudioProcessor::ID_SUB_WEIGHT, 0.0f)),
        static_cast<int>(getVal(AutoLevelDJAudioProcessor::ID_AIR_EXCITER, 0.0f)),
        getVal(AutoLevelDJAudioProcessor::ID_CEILING_DB, -0.3f),
        getVal(AutoLevelDJAudioProcessor::ID_HPF_FREQ, 30.0f),
        getVal(AutoLevelDJAudioProcessor::ID_FREEZE_BREAKDOWNS, 1.0f) > 0.5f ? "true" : "false",
        getVal(AutoLevelDJAudioProcessor::ID_BYPASS, 0.0f) > 0.5f ? "true" : "false"
    );
    return std::string(buf);
}

std::string WebServer::getVisualStateJson() {
    auto vs = m_processor.getVisualState();
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{"
        "\"momentaryLufs\":%.1f,"
        "\"integratedLufs\":%.1f,"
        "\"appliedGainDb\":%.2f,"
        "\"limiterGrDb\":%.2f,"
        "\"peakL\":%.1f,"
        "\"peakR\":%.1f,"
        "\"isFrozen\":%s,"
        "\"mbcGrDb\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f],"
        "\"targetProfile\":%d,"
        "\"bassLiftDb\":%.1f,"
        "\"airLiftDb\":%.1f,"
        "\"deviceInfo\":\"%s\""
        "}",
        vs.loudness.momentaryLUFS,
        vs.loudness.integratedLUFS,
        vs.appliedGainDb,
        vs.limiterGainReductionDb,
        vs.outputPeakDbL,
        vs.outputPeakDbR,
        vs.isFrozen ? "true" : "false",
        vs.mbcGainReductionsDb[0], vs.mbcGainReductionsDb[1], vs.mbcGainReductionsDb[2],
        vs.mbcGainReductionsDb[3], vs.mbcGainReductionsDb[4], vs.mbcGainReductionsDb[5],
        static_cast<int>(vs.activeProfile),
        vs.bassLiftDb,
        vs.airLiftDb,
        m_audioDeviceInfo.c_str()
    );
    return std::string(buf);
}

} // namespace autolevel::web
