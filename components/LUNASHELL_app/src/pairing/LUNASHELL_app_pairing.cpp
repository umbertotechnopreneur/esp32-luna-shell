/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "LUNASHELL_app.hpp"
#include "LUNASHELL_app_internal.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include "esp_log.h"
#include "esp_timer.h"

namespace {
using esp_brookesia::system::core::AppContext;
using LUNASHELL::app_detail::kTag;
using LUNASHELL::app_detail::kEspressifAppStoreUrl;
constexpr char kPairingQrImageId[] = "LUNASHELL.pairing.qr";
constexpr int64_t kPairingTimeoutUs = 5LL * 60LL * 1000000LL;
}

namespace LUNASHELL {
// screen: Typed route to classify as part of the pairing wizard.
bool App::is_pairing_screen(Screen screen)
{
    switch (screen) {
    case Screen::PairIntro:
    case Screen::PairBluetooth:
    case Screen::PairApp:
    case Screen::AppStoreQr:
    case Screen::PairQr:
    case Screen::PairResult:
        return true;
    default:
        return false;
    }
}

// context: Active Brookesia context owning this wizard document.
// payload: Verified QR payload kept alive through synchronous generation.
// Exceptions: std::bad_alloc if GUI/result text allocation fails.
std::expected<void, std::string> App::prepare_pairing_qr(
    AppContext &context,
    const char *payload
)
{
    const esp_err_t generation_result = pairing_qr_.generate(payload);
    if (generation_result != ESP_OK) {
        return std::unexpected(
            "Pairing QR generation failed: " +
            std::string(esp_err_to_name(generation_result))
        );
    }
    if (pairing_qr_registered_) {
        return {};
    }

    const lv_image_dsc_t *image = pairing_qr_.image();
    if (image == nullptr) {
        return std::unexpected(
            "Pairing QR generation completed without an image"
        );
    }
    auto registration_result = context.gui().register_image(
        esp_brookesia::gui::RuntimeImageResource{
            .id = kPairingQrImageId,
            .primary_src = "",
            .native_src = reinterpret_cast<uintptr_t>(image),
            .width = PairingQrAsset::kSize,
            .height = PairingQrAsset::kSize,
            .preload = false,
        }
    );
    if (!registration_result) {
        return std::unexpected(
            "Failed to register pairing QR image: " +
            registration_result.error()
        );
    }
    pairing_qr_registered_ = true;
    return {};
}

// context: Active Brookesia context owning this wizard document.
// Exceptions: std::bad_alloc if GUI/result text allocation fails.
std::expected<void, std::string> App::release_pairing_qr(AppContext &context)
{
    if (pairing_qr_registered_ &&
        !context.gui().unregister_image(kPairingQrImageId)) {
        return std::unexpected("Failed to unregister LUNASHELL pairing QR image");
    }
    pairing_qr_registered_ = false;
    pairing_qr_.reset();
    return {};
}

// context: Active Brookesia context owning this wizard document.
// mode: Explicit navigation mode preserving the existing history policy.
// Exceptions: std::bad_alloc if GUI/result text allocation fails.
std::expected<void, std::string> App::open_app_store_qr(
    AppContext &context,
    NavigationMode mode
)
{
    if (auto result = prepare_pairing_qr(
            context,
            kEspressifAppStoreUrl
        ); !result) {
        return show_pairing_result(
            context,
            result.error(),
            false
        );
    }
    return open_screen(
        context,
        Screen::AppStoreQr,
        "LUNASHELL.open.app_store_qr",
        mode
    );
}

// context: Active Brookesia context owning this wizard document.
// mode: Explicit navigation mode preserving the existing history policy.
// Exceptions: std::bad_alloc if GUI/result text allocation fails.
std::expected<void, std::string> App::start_pairing_session(
    AppContext &context,
    NavigationMode mode
)
{
    const esp_err_t result = LUNASHELL_provisioning_initialized()
        ? LUNASHELL_provisioning_start()
        : LUNASHELL_radios_start_pairing();
    if (result != ESP_OK) {
        return show_pairing_result(
            context,
            "BLE provisioning could not start: " +
                std::string(esp_err_to_name(result)),
            false
        );
    }
    if (auto qr_result = prepare_pairing_qr(
            context,
            LUNASHELL_provisioning_qr_payload()
        ); !qr_result) {
        const esp_err_t stop_result = LUNASHELL_provisioning_stop();
        if (stop_result != ESP_OK) {
            return std::unexpected(
                qr_result.error() +
                "; provisioning stop also failed: " +
                esp_err_to_name(stop_result)
            );
        }
        return show_pairing_result(
            context,
            qr_result.error(),
            false
        );
    }

    pairing_started_at_us_ = esp_timer_get_time();
    return open_screen(
        context,
        Screen::PairQr,
        "LUNASHELL.open.pair_qr",
        mode
    );
}

// context: Active Brookesia context owning this wizard document.
// message: Visible result text from the real provisioning operation.
// success: Whether provisioning actually completed successfully.
// Exceptions: std::bad_alloc if GUI/result text allocation fails.
std::expected<void, std::string> App::show_pairing_result(
    AppContext &context,
    std::string message,
    bool success
)
{
    // The result page no longer owns an active BLE provisioning session.
    if (LUNASHELL_provisioning_initialized()) {
        const auto closure = LUNASHELL_radios_end_pairing(success);
        const esp_err_t stop_result = closure.cleanup_error != ESP_OK ?
            closure.cleanup_error : closure.dma_reservation_error;
        if (stop_result != ESP_OK) {
            return std::unexpected(
                "Failed to stop BLE provisioning before showing its result: " +
                std::string(esp_err_to_name(stop_result))
            );
        }
    }
    pairing_result_ = std::move(message);
    pairing_result_success_ = success;
    pairing_started_at_us_ = 0;
    return open_screen(
        context,
        Screen::PairResult,
        "LUNASHELL.open.pair_result",
        NavigationMode::Replace
    );
}

// context: Active Brookesia context owning this wizard document.
// Exceptions: std::bad_alloc if GUI/result text allocation fails.
std::expected<void, std::string> App::pairing_next(AppContext &context)
{
    const auto mode_for = [this](Screen target) {
        const std::optional<Screen> forward =
            navigation_.forward_target();
        return forward.has_value() && *forward == target
            ? NavigationMode::Forward
            : NavigationMode::Push;
    };

    switch (screen_) {
    case Screen::PairIntro:
        return open_screen(
            context,
            Screen::PairBluetooth,
            "LUNASHELL.open.pair_bluetooth",
            mode_for(Screen::PairBluetooth)
        );
    case Screen::PairBluetooth:
        return open_screen(
            context,
            Screen::PairApp,
            "LUNASHELL.open.pair_app",
            mode_for(Screen::PairApp)
        );
    case Screen::PairApp:
        return open_app_store_qr(
            context,
            mode_for(Screen::AppStoreQr)
        );
    case Screen::AppStoreQr: {
        const NavigationMode mode = mode_for(Screen::PairQr);
        if (!LUNASHELL_provisioning_active()) {
            return start_pairing_session(context, mode);
        }
        if (auto result = prepare_pairing_qr(
                context,
                LUNASHELL_provisioning_qr_payload()
            ); !result) {
            return result;
        }
        return open_screen(
            context,
            Screen::PairQr,
            "LUNASHELL.open.pair_qr",
            mode
        );
    }
    case Screen::PairResult:
        if (pairing_result_success_) {
            return open_screen(
                context,
                Screen::Settings,
                "LUNASHELL.open.settings",
                NavigationMode::ReturnTo
            );
        }
        return start_pairing_session(
            context,
            NavigationMode::Replace
        );
    case Screen::PairQr:
        return std::unexpected(
            "Next action rejected while pairing is in progress"
        );
    default:
        return std::unexpected(
            "Next action rejected outside the pairing wizard"
        );
    }
}

// context: Active Brookesia context owning this wizard document.
// Exceptions: std::bad_alloc if GUI/result text allocation fails.
std::expected<void, std::string> App::refresh_pairing(AppContext &context)
{
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    const char *root = "/LUNASHELL_pairing/surface";
    const char *title = nullptr;
    const char *step = nullptr;
    const char *message = nullptr;
    const char *detail = "";
    const char *accent_color = "#20dfc1";
    bool show_qr = false;
    int32_t progress_value = 0;
    bool progress_hidden = false;
    std::array<char, 192> dynamic_message{};

    switch (screen_) {
    case Screen::PairIntro:
        title = "PAIR DEVICE";
        step = "STEP 1 OF 5";
        message = "Connect LUNASHELL to your Wi-Fi network.";
        detail = "Bluetooth carries the Wi-Fi setup details.";
        progress_value = 1;
        break;
    case Screen::PairBluetooth:
        title = "BLUETOOTH";
        step = "STEP 2 OF 5";
        message = "Turn on Bluetooth on your phone.";
        detail = "Allow Nearby devices when prompted.";
        progress_value = 2;
        break;
    case Screen::PairApp:
        title = "GET THE APP";
        step = "STEP 3 OF 5";
        message = "Install ESP BLE Provisioning on your phone.";
        detail = "Next shows the official Google Play download.";
        progress_value = 3;
        break;
    case Screen::AppStoreQr:
        title = "INSTALL APP";
        step = "STEP 4 OF 5";
        message = "Scan to install ESP BLE Provisioning.";
        detail = "Then return here and press Next.";
        show_qr = true;
        progress_value = 4;
        break;
    case Screen::PairQr: {
        title = "SCAN DEVICE";
        step = "STEP 5 OF 5";
        show_qr = true;
        detail = LUNASHELL_provisioning_service_name();
        progress_value = 5;
        const LUNASHELL_provisioning_state_t state = LUNASHELL_provisioning_state();
        switch (state) {
        case LUNASHELL_PROVISIONING_STATE_IDLE:
        case LUNASHELL_PROVISIONING_STATE_STARTING:
            message = "Starting Bluetooth setup...";
            break;
        case LUNASHELL_PROVISIONING_STATE_WAITING_FOR_APP:
            message = "In the app, scan this device QR.";
            break;
        case LUNASHELL_PROVISIONING_STATE_CREDENTIALS_RECEIVED:
            message = "Wi-Fi details received. Preparing the connection...";
            break;
        case LUNASHELL_PROVISIONING_STATE_CONNECTING:
            message = "Wi-Fi details received. Connecting...";
            break;
        case LUNASHELL_PROVISIONING_STATE_SUCCESS: {
            std::array<char, 128> summary{};
            const esp_err_t summary_result =
                LUNASHELL_provisioning_connection_summary(
                    summary.data(),
                    summary.size()
                );
            if (summary_result == ESP_OK) {
                return show_pairing_result(
                    context,
                    summary.data(),
                    true
                );
            }
            std::snprintf(
                dynamic_message.data(),
                dynamic_message.size(),
                "Wi-Fi connected. Connection summary unavailable (%s).",
                esp_err_to_name(summary_result)
            );
            return show_pairing_result(
                context,
                dynamic_message.data(),
                true
            );
        }
        case LUNASHELL_PROVISIONING_STATE_FAILED:
            switch (LUNASHELL_provisioning_failure_reason()) {
            case LUNASHELL_PROVISIONING_FAILURE_AUTHENTICATION:
                return show_pairing_result(
                    context,
                    "Wi-Fi password was rejected. Check it and try again.",
                    false
                );
            case LUNASHELL_PROVISIONING_FAILURE_ACCESS_POINT_NOT_FOUND:
                return show_pairing_result(
                    context,
                    "Wi-Fi network not found. Check its name or move closer.",
                    false
                );
            case LUNASHELL_PROVISIONING_FAILURE_NONE:
            case LUNASHELL_PROVISIONING_FAILURE_UNKNOWN:
                return show_pairing_result(
                    context,
                    "Wi-Fi connection failed without a reported reason.",
                    false
                );
            }
            return show_pairing_result(
                context,
                "Wi-Fi connection failed with an invalid reason.",
                false
            );
        case LUNASHELL_PROVISIONING_STATE_POISONED:
            return show_pairing_result(
                context,
                "Bluetooth setup stopped after an unrecoverable error.",
                false
            );
        case LUNASHELL_PROVISIONING_STATE_UNAVAILABLE:
            return show_pairing_result(
                context,
                "Wi-Fi setup is unavailable in this firmware build.",
                false
            );
        case LUNASHELL_PROVISIONING_STATE_STOPPING:
        case LUNASHELL_PROVISIONING_STATE_DEINITIALIZING:
            message = "Closing Bluetooth setup...";
            break;
        case LUNASHELL_PROVISIONING_STATE_STOPPED:
            return show_pairing_result(
                context,
                "Bluetooth setup ended before Wi-Fi connected.",
                false
            );
        }

        if (pairing_started_at_us_ > 0 &&
            esp_timer_get_time() - pairing_started_at_us_ >=
                kPairingTimeoutUs) {
            const esp_err_t stop_result = LUNASHELL_provisioning_stop();
            if (stop_result != ESP_OK) {
                std::snprintf(
                    dynamic_message.data(),
                    dynamic_message.size(),
                    "Pairing timed out and BLE could not stop (%s).",
                    esp_err_to_name(stop_result)
                );
            } else {
                std::snprintf(
                    dynamic_message.data(),
                    dynamic_message.size(),
                    "Setup timed out after 5 minutes. Try again."
                );
            }
            return show_pairing_result(
                context,
                dynamic_message.data(),
                false
            );
        }
        break;
    }
    case Screen::PairResult:
        title = pairing_result_success_
            ? "WI-FI READY"
            : "SETUP FAILED";
        step = pairing_result_success_ ? "CONNECTED" : "ACTION REQUIRED";
        message = pairing_result_.c_str();
        detail = pairing_result_success_
            ? "Setup complete. Next returns to Settings."
            : "Press Next to try again.";
        accent_color = pairing_result_success_ ? "#20dfc1" : "#ff3859";
        progress_hidden = true;
        break;
    default:
        return std::unexpected(
            "Pairing refresh rejected outside the wizard"
        );
    }

    const PairingVisualState next_state{
        .screen = screen_,
        .title = title,
        .step = step,
        .message = message,
        .detail = detail,
        .accent_color = accent_color,
        .show_qr = show_qr,
        .progress_value = progress_value,
        .progress_hidden = progress_hidden,
    };
    if (pairing_visual_state_.has_value() &&
        *pairing_visual_state_ == next_state) {
        return {};
    }

    if (auto result = set_text(
            context,
            std::string(root) + "/title",
            title
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            std::string(root) + "/step",
            step
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            std::string(root) + "/message",
            message
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            std::string(root) + "/detail",
            detail
        ); !result) {
        return result;
    }
    if (auto result = set_binding_values(
            context,
            {
                {
                    .absolute_path = std::string(root) + "/step",
                    .key = "color",
                    .value = accent_color,
                },
                {
                    .absolute_path = std::string(root) + "/progress",
                    .key = "value",
                    .value = std::to_string(progress_value),
                },
                {
                    .absolute_path = std::string(root) + "/progress",
                    .key = "hidden",
                    .value = progress_hidden ? "true" : "false",
                },
            }
        ); !result) {
        return result;
    }
    if (auto result = set_hidden(
            context,
            std::string(root) + "/qr",
            !show_qr
        ); !result) {
        return result;
    }
    if (show_qr) {
        if (auto result = set_binding_text(
                context,
                std::string(root) + "/qr",
                "src",
                kPairingQrImageId
            ); !result) {
            return result;
        }
    }
    pairing_visual_state_ = next_state;
    return {};
}


}  // namespace LUNASHELL
