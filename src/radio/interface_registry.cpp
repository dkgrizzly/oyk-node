#include "oyk/radio/interface_registry.hpp"
#include "oyk/radio/axip_interface.hpp"
#include "oyk/radio/axudp_interface.hpp"
#include "oyk/radio/kiss_serial_interface.hpp"
#include "oyk/radio/kiss_tcp_interface.hpp"
#include "oyk/radio/telnet_interface.hpp"

#include <algorithm>
#include <cctype>

namespace oyk::radio {

namespace {
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

InterfaceRegistry::InterfaceRegistry() {
    register_type("axudp", [](const RadioInterfaceConfig& cfg) {
        return std::make_unique<AxudpInterface>(cfg);
    });
    register_type("axip", [](const RadioInterfaceConfig& cfg) {
        return std::make_unique<AxipInterface>(cfg);
    });
    register_type("kiss-serial", [](const RadioInterfaceConfig& cfg) {
        return std::make_unique<KissSerialInterface>(cfg);
    });
    register_type("kiss-tcp", [](const RadioInterfaceConfig& cfg) {
        return std::make_unique<KissTcpInterface>(cfg);
    });
    register_type("telnet", [](const RadioInterfaceConfig& cfg) {
        return std::make_unique<TelnetInterface>(cfg);
    });
}

InterfaceRegistry& InterfaceRegistry::instance() {
    static InterfaceRegistry registry;
    return registry;
}

void InterfaceRegistry::register_type(const std::string& type_name, Factory factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    factories_[to_lower(type_name)] = std::move(factory);
}

bool InterfaceRegistry::has_type(const std::string& type_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return factories_.find(to_lower(type_name)) != factories_.end();
}

std::vector<std::string> InterfaceRegistry::registered_types() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.reserve(factories_.size());
    for (const auto& [name, _] : factories_) {
        out.push_back(name);
    }
    return out;
}

std::unique_ptr<RadioInterface> InterfaceRegistry::create(const RadioInterfaceConfig& config,
                                                          std::string& error_out) const {
    std::string key = to_lower(config.type);
    Factory factory;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = factories_.find(key);
        if (it == factories_.end()) {
            error_out = "Unknown radio interface type: '" + config.type + "'";
            return nullptr;
        }
        factory = it->second;
    }
    return factory(config);
}

} // namespace oyk::radio
