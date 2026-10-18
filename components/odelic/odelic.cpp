#include "odelic.h"
#include "esphome/core/log.h"
#include <mbedtls/aes.h>

namespace esphome::odelic {
static const char *const TAG = "odelic";
static const char *const SERVICE = "9e5d1e47-5c13-43a0-8635-82adffc0386f";
static const char *const COMMAND = "9e5d1e47-5c13-43a0-8635-82adffc1386f";
static const char *const STATUS = "9e5d1e47-5c13-43a0-8635-82adffc2386f";

// These timeouts/retry limits are application policies. The wire framing and
// reconstructed crypto are documented in odelic-research/docs/login-protocol.md.
static constexpr uint32_t ATT_WRITE_TIMEOUT_MS = 3'000;
static constexpr uint32_t TRANSACTION_TIMEOUT_MS = 4'000;
static constexpr uint32_t MESH_REPLY_ADDRESS_TIMEOUT_MS = 3'000;
static constexpr uint32_t FIXTURE_SETTLE_MS = 500;  // Allow an applied setting to settle before querying it.
static constexpr uint8_t MAX_COMMAND_ATTEMPTS = 3;

void OdelicHub::suspend_for_setup() {
  setup_suspended_ = true;
  parent()->set_auto_connect(false);
  parent()->set_enabled(false);
  invalidate_();
  intent_ = {};
  have_mask_ = have_own_ = have_fixture_ = false;
  write_pending_ = login_reply_pending_ = false;
  command_handle_ = notify_handle_ = cccd_handle_ = 0;
  phase_(Phase::DISCONNECTED);
}
void OdelicHub::resume_after_setup() {
  setup_suspended_ = false;
  poll_due_ = true;
  parent()->set_auto_connect(true);
  parent()->set_enabled(true);
}
void OdelicHub::set_lighting_id(const std::string &id) {
  // The app splits the eight digits: first four form a decimal home ID encoded
  // little-endian; the remaining four remain ASCII password bytes. BLE MAC,
  // network ID, and changing mesh virtual addresses serve different purposes.
  unsigned home = 0;
  for (size_t i = 0; i < 4; i++)
    home = home * 10 + (id[i] - '0');
  for (size_t i = 0; i < 4; i++) {
    home_[i] = home >> (8 * i);
    password_[i] = id[i + 4];
  }
}
void OdelicHub::setup() { invalidate_(); }
void OdelicHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Odelic BLE light: %s; unit %u", parent()->address_str(), unit_);
  LOG_UPDATE_INTERVAL(this);
}
void OdelicHub::phase_(Phase phase) {
  phase_state_ = phase;
  phase_at_ = millis();
}
void OdelicHub::invalidate_() {
  actual_ = {};
  fresh_ = false;
  if (valid_sensor_ != nullptr)
    valid_sensor_->publish_state(false);
  status_set_warning();
}
void OdelicHub::fail_(const char *reason) {
  ESP_LOGW(TAG, "%s", reason);
  invalidate_();
  intent_ = {};
  have_mask_ = have_own_ = have_fixture_ = false;
  write_pending_ = login_reply_pending_ = false;
  phase_(Phase::DISCONNECTED);
  parent()->disconnect();
}
bool OdelicHub::write_(const uint8_t *data, size_t size) {
  if (write_pending_ || command_handle_ == 0)
    return false;
  auto error = esp_ble_gattc_write_char(parent()->get_gattc_if(), parent()->get_conn_id(), command_handle_, size,
                                        const_cast<uint8_t *>(data), ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (error != ESP_OK) {
    fail_("GATT write could not be queued");
    return false;
  }
  write_pending_ = true;
  write_at_ = millis();
  return true;
}
void OdelicHub::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t interface,
                                    esp_ble_gattc_cb_param_t *param) {
  if (setup_suspended_)
    return;
  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      invalidate_();
      intent_ = {};
      have_mask_ = have_own_ = have_fixture_ = false;
      write_pending_ = login_reply_pending_ = false;
      command_handle_ = notify_handle_ = cccd_handle_ = 0;
      phase_(Phase::DISCONNECTED);
      break;
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *command = parent()->get_characteristic(esp32_ble_tracker::ESPBTUUID::from_raw(SERVICE),
                                                   esp32_ble_tracker::ESPBTUUID::from_raw(COMMAND));
      auto *status = parent()->get_characteristic(esp32_ble_tracker::ESPBTUUID::from_raw(SERVICE),
                                                  esp32_ble_tracker::ESPBTUUID::from_raw(STATUS));
      if (command == nullptr || status == nullptr) {
        fail_("Required Odelic service missing");
        break;
      }
      auto *descriptor = status->get_descriptor(esp32_ble_tracker::ESPBTUUID::from_uint16(0x2902));
      if (descriptor == nullptr) {
        fail_("Notification descriptor missing");
        break;
      }
      command_handle_ = command->handle;
      notify_handle_ = status->handle;
      cccd_handle_ = descriptor->handle;
      phase_(Phase::SUBSCRIBING);
      if (parent()->register_for_notify(notify_handle_) != ESP_OK)
        fail_("Notification registration failed");
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle == notify_handle_) {
        if (param->reg_for_notify.status != ESP_GATT_OK) {
          fail_("Notification registration rejected");
          break;
        }
        node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
        // The ESPHome parent enables CCCD. Wait for its completion before login.
      }
      break;
    case ESP_GATTC_WRITE_DESCR_EVT:
      if (phase_state_ == Phase::SUBSCRIBING && param->write.handle == cccd_handle_) {
        if (param->write.status != ESP_GATT_OK) {
          fail_("Notification subscription rejected");
          break;
        }
        phase_(Phase::CHALLENGE);
        const uint8_t request[] = {1, 0x20};
        write_(request, sizeof(request));
      }
      break;
    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == command_handle_) {
        write_pending_ = false;
        if (param->write.status != ESP_GATT_OK)
          fail_("GATT write rejected");
      }
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == notify_handle_ && phase_state_ != Phase::DISCONNECTED)
        notify_(param->notify.value, param->notify.value_len);
      break;
    default:
      break;
  }
}
bool OdelicHub::login_response_(const uint8_t *challenge) {
  // App-compatible AES-128 key: interleave the four home/password bytes, then
  // append LOGINKEY. The challenge yields the per-session XOR mask. The reply
  // proves knowledge of the same credentials and includes that mask plus padding.
  // This is the fixture login protocol, unrelated to ESPHome native API encryption.
  uint8_t key[16], plain[16], response[19] = {1, 0x16, 3}, answer[16];
  for (size_t i = 0; i < 4; i++) {
    key[2 * i] = home_[i];
    key[2 * i + 1] = password_[i];
  }
  memcpy(key + 8, "LOGINKEY", 8);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int error = mbedtls_aes_setkey_dec(&aes, key, 128);
  if (!error)
    error = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, challenge, plain);
  if (error || memcmp(plain, home_.data(), 4)) {
    mbedtls_aes_free(&aes);
    return false;
  }
  memcpy(mask_.data(), plain + 4, 4);
  have_mask_ = true;
  memcpy(answer, home_.data(), 4);
  memcpy(answer + 4, password_.data(), 4);
  memcpy(answer + 8, plain + 4, 4);
  memset(answer + 12, 4, 4);
  error = mbedtls_aes_setkey_enc(&aes, key, 128);
  if (!error)
    error = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, answer, response + 3);
  mbedtls_aes_free(&aes);
  if (error)
    return false;
  // Send only after the challenge-request ATT write completes.
  memcpy(login_reply_.data(), response, sizeof(response));
  login_reply_pending_ = true;
  return true;
}
size_t OdelicHub::decrypt_event_(const uint8_t *data, size_t size, uint8_t *plain) {
  // Notifications have a six-byte outer header followed by AES blocks. Undo the
  // repeating four-byte session mask, then decrypt with the EVENTKEY variant.
  // Reject malformed length/padding instead of accepting random bytes as state.
  if (!have_mask_ || size < 22 || size > 246 || (size - 6) % 16)
    return 0;
  uint8_t key[16], cipher[240];
  for (size_t i = 0; i < 4; i++) {
    key[2 * i] = home_[i];
    key[2 * i + 1] = password_[i];
  }
  memcpy(key + 8, "EVENTKEY", 8);
  const size_t length = size - 6;
  for (size_t i = 0; i < length; i++)
    cipher[i] = data[i + 6] ^ mask_[i % 4];
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int error = mbedtls_aes_setkey_dec(&aes, key, 128);
  for (size_t i = 0; i < length && !error; i += 16)
    error = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, cipher + i, plain + i);
  mbedtls_aes_free(&aes);
  if (error)
    return 0;
  const uint8_t padding = plain[length - 1];
  if (padding < 1 || padding > 16)
    return 0;
  for (size_t i = length - padding; i < length; i++)
    if (plain[i] != padding)
      return 0;
  return length - padding;
}
void OdelicHub::notify_(const uint8_t *data, size_t size) {
  if (size == 18 && data[0] == 2 && data[1] == 0x20 && phase_state_ == Phase::CHALLENGE) {
    if (!login_response_(data + 2))
      fail_("Login challenge did not match configured network");
    return;
  }
  if (size == 3 && data[0] == 2 && data[1] == 0x16 && phase_state_ == Phase::LOGIN) {
    if (data[2] != 0) {
      fail_("Login rejected");
      return;
    }
    ESP_LOGI(TAG, "Login accepted");
    poll_due_ = true;
    phase_(Phase::READY);
    return;
  }
  if (size == 6 && data[0] == 1 && data[1] == 0x0a) {
    if (have_own_ && memcmp(own_.data(), data + 2, 4)) {
      have_fixture_ = false;
      fresh_ = false;
      poll_due_ = true;
      if (phase_state_ == Phase::DISCOVERING || phase_state_ == Phase::QUERYING)
        phase_(Phase::READY);
    }
    memcpy(own_.data(), data + 2, 4);
    have_own_ = true;
    return;
  }
  if (size < 22 || data[0] != 6 || !have_own_ || memcmp(data + 1, own_.data(), 4))
    return;
  uint8_t plain[240];
  size_t length = decrypt_event_(data, size, plain);
  if (!length)
    return;
  if (data[5] == 0xff && length >= 14 && phase_state_ == Phase::DISCOVERING) {
    // A shared network can report several fixtures. Bind the freshly discovered
    // virtual address only when the reply embeds the selected Bluetooth MAC.
    uint64_t mac = parent()->get_address();
    for (size_t i = 0; i < 6; i++)
      if (plain[i] != ((mac >> (8 * i)) & 255))
        return;
    memcpy(fixture_.data(), plain + 6, 4);
    have_fixture_ = true;
    return;
  }
  if (data[5] == 0x27 && have_fixture_ && phase_state_ == Phase::QUERYING &&
      protocol::parse_status(plain, length, fixture_.data(), actual_)) {
    fresh_ = true;
    poll_due_ = false;
    phase_(Phase::READY);
    publish_();
  }
}
void OdelicHub::start_query_() {
  if (!have_own_ || write_pending_)
    return;
  uint8_t ping[] = {3, 255, 255, 255, 255, 0xfe, 0, 0, 0, 0};
  memcpy(ping + 6, own_.data(), 4);
  have_fixture_ = false;
  fresh_ = false;
  phase_(Phase::DISCOVERING);
  write_(ping, sizeof(ping));
}
void OdelicHub::publish_() {
  ++state_revision_;
  status_clear_warning();
  if (valid_sensor_ != nullptr)
    valid_sensor_->publish_state(true);
  if (light_ != nullptr)
    light_->publish(actual_);
  if (preset_ != nullptr && actual_.levels) {
    bool matched = false;
    for (size_t i = 0; i < 4; i++)
      if (actual_.color == protocol::PRESETS[i]) {
        preset_->publish_state(i);
        matched = true;
      }
    if (!matched)
      preset_->set_has_state(false);
  }
  if (actual_.levels)
    ESP_LOGI(TAG, "Confirmed ON color=%u brightness=%u%%", actual_.color, 100 - actual_.dimming * 5);
  else
    ESP_LOGI(TAG, "Confirmed %s", actual_.on ? "ON (levels unavailable)" : "OFF");
}
void OdelicHub::request_light(bool on, float brightness) {
  if (setup_suspended_)
    return;
  if (!actual_.known || phase_state_ == Phase::DISCONNECTED) {
    ESP_LOGW(TAG, "Command refused: no confirmed fixture state");
    return;
  }
  intent_ = {protocol::Kind::LIGHT, on, protocol::quantize(brightness), 0};
  attempts_ = 0;
  fresh_ = false;
  poll_due_ = true;
}
void OdelicHub::request_preset(size_t index) {
  if (setup_suspended_)
    return;
  if (index >= 4 || !actual_.known || !actual_.on) {
    ESP_LOGW(TAG, "Preset requires confirmed ON state");
    return;
  }
  intent_ = {protocol::Kind::PRESET, true, 20, protocol::PRESETS[index]};
  attempts_ = 0;
  fresh_ = false;
  poll_due_ = true;
}
void OdelicHub::apply_() {
  auto plan = protocol::plan(actual_, intent_);
  if (plan.action == protocol::Action::DONE) {
    intent_ = {};
    attempts_ = 0;
    return;
  }
  if (plan.action == protocol::Action::REJECT || attempts_ >= MAX_COMMAND_ATTEMPTS) {
    ESP_LOGW(TAG, "Command could not be confirmed or current levels unavailable");
    intent_ = {};
    return;
  }
  std::array<uint8_t, 18> packet;
  if (!protocol::setting(fixture_.data(), own_.data(), unit_, plan.color, plan.dimming, packet)) {
    intent_ = {};
    return;
  }
  if (write_(packet.data(), packet.size())) {
    attempts_++;
    fresh_ = false;
    phase_(Phase::SETTLING);
  }
}
void OdelicHub::loop() {
  if (setup_suspended_)
    return;
  if (phase_state_ == Phase::DISCONNECTED)
    return;
  if (write_pending_ && millis() - write_at_ > ATT_WRITE_TIMEOUT_MS) {
    fail_("GATT write acknowledgment timeout");
    return;
  }
  // Unsigned subtraction also handles millis() wrapping between state changes.
  const uint32_t elapsed = millis() - phase_at_;
  if (phase_state_ != Phase::READY && elapsed > TRANSACTION_TIMEOUT_MS) {
    fail_("Odelic transaction timeout");
    return;
  }
  if (write_pending_)
    return;
  if (phase_state_ == Phase::CHALLENGE && login_reply_pending_) {
    login_reply_pending_ = false;
    phase_(Phase::LOGIN);
    write_(login_reply_.data(), login_reply_.size());
    return;
  }
  if (phase_state_ == Phase::DISCOVERING && have_fixture_) {
    uint8_t query[] = {3, 0, 0, 0, 0, 0x20, 0, 0, 0, 0, 0x70};
    memcpy(query + 1, fixture_.data(), 4);
    memcpy(query + 6, own_.data(), 4);
    phase_(Phase::QUERYING);
    write_(query, sizeof(query));
  } else if (phase_state_ == Phase::SETTLING && elapsed >= FIXTURE_SETTLE_MS) {
    start_query_();
  } else if (phase_state_ == Phase::READY) {
    if (!have_own_) {
      if (elapsed > MESH_REPLY_ADDRESS_TIMEOUT_MS)
        fail_("No mesh reply address received");
      return;
    }
    if (intent_.kind != protocol::Kind::NONE && fresh_)
      apply_();
    else if (poll_due_ || (intent_.kind != protocol::Kind::NONE && !fresh_))
      start_query_();
  }
}
light::LightTraits OdelicLight::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  return traits;
}
void OdelicLight::update_state(light::LightState *state) {
  if (have_confirmed_) {
    if (sink_ != nullptr)
      sink_->light_command(slot_, state->remote_values.is_on(), state->remote_values.get_brightness());
    else
      parent_->request_light(state->remote_values.is_on(), state->remote_values.get_brightness());
  }
  // LightCall publishes after update_state for instant calls. Keep that publication
  // at the last confirmed state until our separate fixture query returns.
  state->remote_values = confirmed_;
  state->current_values = confirmed_;
}
void OdelicLight::publish(const protocol::Status &actual) {
  if (state_ == nullptr)
    return;
  confirmed_.set_color_mode(light::ColorMode::BRIGHTNESS);
  confirmed_.set_state(actual.on);
  if (actual.levels)
    confirmed_.set_brightness((100 - actual.dimming * 5) / 100.0f);
  else if (!have_confirmed_)
    confirmed_.set_brightness(1.0f);
  have_confirmed_ = true;
  state_->remote_values = confirmed_;
  state_->current_values = confirmed_;
  state_->publish_state();
}
}  // namespace esphome::odelic
