# Startup composition

Selects and starts the configured firmware profile and its admitted services.
Keep boot orchestration here; display, sensors, storage, radio and audio retain
their component owners. Optional startup failures must remain visible in the
canonical snapshot, not be converted into successful component results.

See [startup](../docs/architecture/startup-and-logger.md) and
[profile configuration](../docs/architecture/board-build-configuration.md).
