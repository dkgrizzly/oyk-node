// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/radio/radio_interface.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace oyk::radio {

// Modular factory registry for radio interface drivers.
class InterfaceRegistry {
public:
    using Factory = std::function<std::unique_ptr<RadioInterface>(const RadioInterfaceConfig&)>;

    static InterfaceRegistry& instance();

    void register_type(const std::string& type_name, Factory factory);
    bool has_type(const std::string& type_name) const;
    std::vector<std::string> registered_types() const;

    std::unique_ptr<RadioInterface> create(const RadioInterfaceConfig& config,
                                           std::string& error_out) const;

private:
    InterfaceRegistry();

    mutable std::mutex mutex_;
    std::map<std::string, Factory> factories_;
};

} // namespace oyk::radio
