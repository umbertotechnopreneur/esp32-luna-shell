"""Host preference persistence; unreadable state is never a first-run default.

A failure disables writes for this owner lifetime. Repairing or moving the file
requires an explicit external action and a new Studio window before saving can
resume. Captures, baseline files and native settings are not owned here.
"""

import json
import logging
from pathlib import Path
from uuid import uuid4

from runtime_paths import resolve_runtime_output

SCHEMA = 'cody.studio.ui.v1'
ATLAS_SUITES = ('Pilot (10)', 'Supported screens', 'Filtered catalog')
_ERRORS = (OSError, ValueError, RuntimeError)
_LOGGER = logging.getLogger(__name__)


# pairs: Decoded JSON members before dictionary construction at each nesting level.
# Raises ValueError for duplicate keys rather than silently discarding a value.
def _unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f'Duplicate Studio UI preference: {key}')
        value[key] = item
    return value


# value: Parsed or proposed host preferences; optional absent fields keep UI defaults.
# Raises ValueError for unknown fields, unsupported schemas or invalid field types.
def validate_preferences(value):
    fields = {'schema', 'expanders', 'left', 'right', 'atlas_suite', 'baseline'}
    if not isinstance(value, dict) or value.get('schema') != SCHEMA:
        raise ValueError('Unsupported Studio UI preferences schema')
    if set(value) - fields:
        raise ValueError(f'Unknown Studio UI preferences: {sorted(set(value) - fields)}')
    for key in ('left', 'right'):
        if key in value and type(value[key]) is not bool:
            raise ValueError(f'Studio UI preference {key} must be boolean')
    if 'expanders' in value:
        expanders = value['expanders']
        if (not isinstance(expanders, dict) or
                any(not isinstance(key, str) or not key or type(opened) is not bool
                    for key, opened in expanders.items())):
            raise ValueError('Studio UI expanders require nonempty names and boolean values')
    if 'atlas_suite' in value and value['atlas_suite'] not in ATLAS_SUITES:
        raise ValueError(f'Invalid Studio UI atlas_suite: {value["atlas_suite"]!r}')
    baseline = value.get('baseline')
    if baseline is not None and (not isinstance(baseline, str) or not baseline.strip()):
        raise ValueError('Studio UI baseline must be a nonempty path or null')


class PreferenceStore:
    """One window's read/write admission, with a retained original failure."""

    # path: Host preference file beneath the simulator's artifact root.
    # Raises TypeError for an invalid path argument; performs no filesystem writes.
    def __init__(self, path):
        self.path = Path(path)
        self.error = None
        self._loaded = False

    # No parameters: resolve the destination without treating a dangling link as absent.
    # Raises OSError, ValueError or RuntimeError for unsafe or unresolvable destinations.
    def _destination(self):
        if self.path.is_symlink():
            raise ValueError(f'Studio UI preferences must not be a symbolic link: {self.path}')
        return resolve_runtime_output(self.path)

    # path: Resolved artifact destination to read and validate without repairing it.
    # Raises original I/O, decoding or validation errors; only absence returns defaults.
    def _read(self, path):
        try:
            content = path.read_text(encoding='utf-8')
        except FileNotFoundError:
            # A present directory entry with a missing target is not first run.
            try:
                path.lstat()
            except FileNotFoundError:
                return {}
            raise
        value = json.loads(content, object_pairs_hook=_unique_object)
        validate_preferences(value)
        return value

    # operation: Failed load or save, included with the original cause in the host log.
    # error: Original filesystem, decoding or schema failure retained for this session.
    # Raises no exceptions under normal logging configuration; never changes stored data.
    def _block(self, operation, error):
        if self.error is None:
            self.error = error
            _LOGGER.error('Studio preferences %s failed for %s; saving disabled: %s',
                          operation, self.path, error)

    # No parameters: read optional preferences before enabling any automatic save.
    # Raises the original error, including on repeated calls after external repair.
    def load(self):
        if self.error is not None:
            raise self.error
        try:
            value = self._read(self._destination())
        except _ERRORS as error:
            self._block('load', error)
            raise
        self._loaded = True
        return value

    # value: Complete proposed preferences, validated before staging any output.
    # Raises the original error for a blocked owner or failed read/write/validation.
    # Raises RuntimeError if load has not succeeded, preventing blind replacement.
    def save(self, value):
        if self.error is not None:
            raise self.error
        if not self._loaded:
            raise RuntimeError('Studio UI preferences must be loaded before saving')
        try:
            validate_preferences(value)
            path = self._destination()
            # Detect a newly unreadable file even when startup found valid or absent state.
            self._read(path)
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = path.with_name(f'{path.name}.{uuid4().hex}.tmp')
            # Unique staging avoids overwriting a partial write retained after failure.
            with temporary.open('x', encoding='utf-8') as output:
                json.dump(value, output, indent=2)
                output.write('\n')
            temporary.replace(path)
        except _ERRORS as error:
            self._block('save', error)
            raise
