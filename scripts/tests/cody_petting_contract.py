"""Independent host-preview gesture contracts; these are not native trace parity."""

from simulator.windows.cody_petting import PettingRecognizer, PettingZone, load_petting_config


# recognizer: Actual simulator recognizer retaining contact/cooldown state.
# scene: Explicit synthetic rectangle matching the native gesture fixture.
# at_us: Original acquired press timestamp for this stroke.
# y: Selected upper or lower body region.
def slow_stroke(recognizer, scene, at_us, y):
    results = [recognizer.observe(scene, (220., y), True, True, at_us)]
    for index in range(1, 7):
        results.append(recognizer.observe(scene, (220. + index * 4., y), True,
                                          False, at_us + index * 100_000))
    return results


# root: Checkout containing the shared native authored gesture constants.
# Raises AssertionError for behavioral regressions or the original config/geometry error.
def check_host_petting(root):
    config = load_petting_config(root)
    scene = {
        "scale": 145.,
        "world": {"eye.left": (200., 200.), "eye.right": (260., 200.),
                  "mouth": (230., 260.)},
        "paths": [{"definition": {"name": "body"},
                   "vertices": [(100., 100.), (360., 100.), (360., 360.), (100., 360.)]}],
    }
    assertions = 0

    # condition: Independent expected outcome from a synthetic acquired gesture.
    # message: Human-readable contract failure.
    # Raises AssertionError when the production host recognizer violates the outcome.
    def require(condition, message):
        nonlocal assertions
        assertions += 1
        if not condition:
            raise AssertionError(message)

    recognizer = PettingRecognizer(config)
    results = slow_stroke(recognizer, scene, 1_000_000, 150.)
    require(all(result == PettingZone.NONE for result in results[:4]),
            "A short stroke must wait for sufficient duration and path")
    require(results.count(PettingZone.ABOVE_EYES) == 1, "Gentle upper-body stroke must recognize once")
    require(recognizer.observe(scene, (248., 150.), True, False, 1_700_000) == PettingZone.NONE,
            "Same contact must not recognize twice")
    require(recognizer.observe(scene, (248., 150.), False, False, 1_800_000) == PettingZone.NONE
            and not recognizer.active, "Release must end the contact without a reaction")
    require(all(result == PettingZone.NONE for result in slow_stroke(recognizer, scene, 2_000_000, 290.)),
            "New contact during cooldown must remain suppressed")
    require(slow_stroke(recognizer, scene, 8_000_000, 290.).count(PettingZone.BELOW_MOUTH) == 1,
            "Gentle lower-body stroke must recognize after cooldown")

    recognizer = PettingRecognizer(config)
    recognizer.observe(scene, (220., 150.), True, True, 1_000_000)
    require(recognizer.observe(scene, (244., 150.), False, False, 1_100_000) == PettingZone.NONE,
            "A tap and release-coordinate jump must not become a caress")

    recognizer = PettingRecognizer(config)
    recognizer.observe(scene, (220., 150.), True, True, 1_000_000)
    require(recognizer.observe(scene, (270., 150.), True, False, 1_050_000) == PettingZone.NONE
            and recognizer.rejected, "Fast swipe must reject the complete contact")

    recognizer = PettingRecognizer(config)
    recognizer.observe(scene, (220., 150.), True, True, 1_000_000)
    recognizer.observe(scene, (221., 150.), True, False, 1_002_000)
    require(recognizer.observe(scene, (222., 150.), True, False, 1_004_000) == PettingZone.NONE
            and recognizer.rejected, "Dense sub-jitter reports must not disguise excessive speed")

    for y, label in ((230., "face middle band"), (80., "outside actual body")):
        recognizer = PettingRecognizer(config)
        require(all(result == PettingZone.NONE for result in slow_stroke(recognizer, scene, 1_000_000, y)),
                f"Stroke in {label} must remain excluded")

    recognizer = PettingRecognizer(config)
    recognizer.observe(scene, (220., 150.), True, True, 1_000_000)
    require(recognizer.observe(scene, (224., 150.), True, False, 1_400_000) == PettingZone.NONE
            and recognizer.rejected, "Missing acquisition interval must invalidate contact")

    recognizer = PettingRecognizer(config)
    recognizer.observe(scene, (220., 150.), True, True, 1_000_000)
    recognizer.observe(scene, (224., 150.), True, False, 1_100_000)
    recognizer.cancel()
    require(recognizer.observe(scene, (248., 150.), True, False, 1_200_000) == PettingZone.NONE
            and not recognizer.active, "Cancelled contact must not resume without a new press edge")

    recognizer = PettingRecognizer(config)
    recognizer.observe(scene, (220., 150.), True, True, 1_000_000)
    for index in range(1, 9):
        require(recognizer.observe(scene, (220. + index % 2, 150.), True,
                                   False, 1_000_000 + index * 100_000) == PettingZone.NONE,
                "Stationary noisy contact must not recognize a caress")

    recognizer = PettingRecognizer(config)
    recognizer.observe(scene, (220., 150.), True, True, 1_000_000)
    for index in range(1, 11):
        require(recognizer.observe(scene, (220. + (index % 2) * 4., 150.), True,
                                   False, 1_000_000 + index * 100_000) == PettingZone.NONE,
                "Accumulated local rubbing without enough net displacement must not qualify")

    recognizer = PettingRecognizer(config)
    recognizer.observe(scene, (220., 150.), True, True, 1_000_000)
    require(recognizer.observe(scene, (224., 290.), True, False, 1_100_000) == PettingZone.NONE
            and recognizer.rejected, "Crossing between petting zones must invalidate contact")
    return assertions
