# Power lifecycle

Coordinates power inputs, display/audio standby and wake, real inactivity, and
transient scene standby overrides. The stub serves builds without power inputs.

Scene overrides are bounded intent, not persisted settings or synthetic activity;
the power owner applies the existing lifecycle transitions.
