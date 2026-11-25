#pragma once

#include <winrt/base.h>

namespace mdsp {

class Apartment {
public:
    explicit Apartment(winrt::apartment_type type) { winrt::init_apartment(type); }
    ~Apartment() { winrt::uninit_apartment(); }

    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;
};

} // namespace mdsp
