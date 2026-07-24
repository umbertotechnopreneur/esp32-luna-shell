#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace qsee {

class App final : public esp_brookesia::systems::phone::App {
public:
    static App *requestInstance();
    ~App() override = default;

protected:
    App();
    bool run() override;
    bool back() override;

private:
    static App *_instance;
};

} // namespace qsee
