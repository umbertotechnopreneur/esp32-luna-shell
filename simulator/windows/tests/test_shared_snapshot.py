"""Synthetic boundaries exercising the actual firmware decoder, aging and JSON."""
import unittest
from unittest.mock import patch

from device_model import ScenarioEngine, SemanticActionError, validate_snapshot


class SharedSnapshotTests(unittest.TestCase):
    # self: Actual host-native transport whose FFI operations are observed without replacing its policy.
    # Raises AssertionError if a generation returns to per-leaf imports or per-component metadata reads.
    def test_snapshot_uses_one_import_and_one_metadata_projection(self):
        from cody_native_snapshot import NativeSnapshot
        source = ScenarioEngine.from_scenario("healthy").snapshot()
        baseline = NativeSnapshot(source, 1000000)
        api = baseline.api
        with patch.object(api, "cody_host_snapshot_import", wraps=api.cody_host_snapshot_import) as importing:
            with patch.object(api, "cody_host_snapshot_components", wraps=api.cody_host_snapshot_components) as projecting:
                owner = NativeSnapshot(source, 1000000)
                projected = owner.aged_mapping(source)
                self.assertEqual(importing.call_count, 1)
                self.assertEqual(projecting.call_count, 1)
        self.assertEqual(projected, source)

    # self: Python text boundary before C string marshalling can truncate an original value.
    # Raises AssertionError if embedded NUL is silently accepted as a shorter literal.
    def test_snapshot_literal_rejects_embedded_nul(self):
        from cody_native_snapshot import literal
        with self.assertRaises(ValueError):
            literal("visible\0hidden")

    # self: Deterministic native observation with no fresh acquisition after its initial timestamp.
    # Raises AssertionError if virtual time fabricates a new sample or Python JSON exposes stale numbers.
    def test_aging_preserves_measurement_and_native_json_masks_stale_value(self):
        engine = ScenarioEngine.from_scenario("healthy")
        timestamp = engine.snapshot()["battery"]["last_success_at_us"]
        # The native owner is independent of the timeline's scheduled acquisitions.
        from cody_native_snapshot import NativeSnapshot
        owner = NativeSnapshot(engine.snapshot(), timestamp + 5000000)
        aged = owner.aged_mapping(engine.snapshot())["battery"]
        self.assertFalse(aged["data_valid"])
        self.assertTrue(aged["data_stale"])
        self.assertEqual(aged["last_success_at_us"], timestamp)
        payload = owner.api_payload("/api/v1/info")
        self.assertIsNone(payload["data"]["battery"]["level_percent"])
        self.assertNotIn("ssid", payload["data"]["radios"]["wifi"])

    # self: Host scenario with no acquisition while observation time crosses the firmware TTL.
    # Raises AssertionError or validation errors if historical observations require invented freshness.
    def test_aged_generation_remains_a_valid_historical_observation(self):
        engine = ScenarioEngine.from_scenario("healthy")
        original = engine.snapshot()
        engine.advance_ms(5000)
        aged = engine.snapshot()
        validate_snapshot(aged, now_us=engine.clock.now_us)
        self.assertFalse(aged["battery"]["data_valid"])
        self.assertTrue(aged["gps"]["data_stale"])
        self.assertEqual(aged["gps"]["last_success_at_us"], original["gps"]["last_success_at_us"])
        self.assertEqual(aged["gps"]["latitude_degrees"], original["gps"]["latitude_degrees"])

    # self: Two synthetic RTC acquisitions, one good and one malformed BCD.
    # Raises AssertionError if decoder failure replaces the historical sample or is reported as success.
    def test_rtc_register_action_uses_production_decoder_and_preserves_failure(self):
        engine = ScenarioEngine.from_scenario("healthy")
        system_time = engine.snapshot()["rtc"]["system_time"]
        engine.advance_ms(1)
        engine.action("rtc-registers", registers=[0x59, 0x58, 0x23, 0x29, 0, 0x02, 0x24])
        previous = engine.snapshot()["rtc"]
        self.assertEqual(previous["system_time"], system_time)
        self.assertEqual((previous["year"], previous["month"], previous["day"]), (2024, 2, 29))
        with self.assertRaises(SemanticActionError):
            engine.action("rtc-registers", registers=[0x59, 0x6a, 0x23, 0x29, 0, 0x02, 0x24])
        failed = engine.snapshot()["rtc"]
        self.assertEqual(failed["last_error"], "ESP_ERR_INVALID_RESPONSE")
        self.assertFalse(failed["data_valid"])
        self.assertEqual(failed["last_success_at_us"], previous["last_success_at_us"])
        self.assertEqual(failed["year"], previous["year"])

    # self: Host-to-native clock boundary independent of library availability.
    # Raises AssertionError if Python integer narrowing can wrap the observation clock.
    def test_native_observation_rejects_clock_overflow_before_loading(self):
        from cody_native_snapshot import NativeSnapshot
        with patch("cody_native_snapshot.load_runtime_library") as loader:
            for value in (True, 0, -1, 1 << 63, 1 << 64, 1.0):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    NativeSnapshot({}, value)
            loader.assert_not_called()

    # self: A synthetic semantic action whose native publication fails before commit.
    # Raises AssertionError if acquisition, virtual clock or exposed generation partially advances.
    def test_failed_native_publication_does_not_commit_candidate_state(self):
        engine = ScenarioEngine.from_scenario("healthy")
        before = engine.generation
        original = engine.snapshot()
        with patch("cody_native_snapshot.NativeSnapshot", side_effect=RuntimeError("native sink unavailable")):
            with self.assertRaises(RuntimeError):
                engine.action("press:boot")
        self.assertIs(engine.generation, before)
        self.assertEqual(engine.snapshot(), original)

    # self: Synthetic standby owner with a pending real C++ fault configuration.
    # Raises AssertionError if failed publication consumes a fault or replaces the committed owner.
    def test_standby_candidate_commits_only_with_its_snapshot(self):
        engine = ScenarioEngine.from_scenario("healthy")
        engine.action("standby:fault", resource="distance", target=True,
                      error="ESP_ERR_TIMEOUT", count=1)
        owner = engine._native_standby
        before = owner.snapshot()
        generation = engine.generation
        snapshot = engine.snapshot()
        with patch("cody_native_snapshot.NativeSnapshot", side_effect=RuntimeError("publication unavailable")):
            with self.assertRaises(RuntimeError):
                engine.action("press:pwr")
            with self.assertRaises(RuntimeError):
                engine.action("standby:profile", deep_sleep_enabled=True,
                              retained_connection=True, voice_listening=False,
                              touch_wake_available=True)
            with self.assertRaises(RuntimeError):
                engine.action("standby:fault", resource="gps", target=True,
                              error="ESP_FAIL", count=2)
        self.assertIs(engine._native_standby, owner)
        self.assertEqual(owner.snapshot(), before)
        self.assertIs(engine.generation, generation)
        self.assertEqual(engine.snapshot(), snapshot)
        # The fault consumed only by the discarded candidate remains pending.
        with self.assertRaisesRegex(SemanticActionError, "ESP_ERR_TIMEOUT"):
            engine.action("press:pwr")
        self.assertEqual(engine.snapshot()["power"]["last_error"], "ESP_ERR_TIMEOUT")
        self.assertEqual(engine.standby_diagnostics()["error_code"], 0x107)

    # self: Actual host HTTP output without an ESP-IDF Task-WDT owner.
    # Raises AssertionError if the simulator fabricates measured firmware watchdog health.
    def test_liveness_is_explicitly_unavailable_on_host(self):
        payload = ScenarioEngine.from_scenario("healthy").api_payload("/api/v1/health")
        self.assertFalse(payload["data"]["liveness"]["available"])
        self.assertEqual(payload["data"]["liveness"]["query_error"], "ESP_ERR_NOT_SUPPORTED")
        self.assertTrue(payload["simulator"]["synthetic"])
        self.assertEqual(payload["simulator"]["runtime"], "production-cpp")
