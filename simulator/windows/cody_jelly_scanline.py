"""Pearlescent cloud sprite with host-only inverse scanline deformation."""

from __future__ import annotations

from dataclasses import dataclass
import math

from PIL import Image

from scripts.common.cody_rig_contract import evaluate
from cody_png_mesh import bind_weights


RED_BLUE_LEVELS = tuple(round(value * 31 / 255) * 255 // 31 for value in range(256))
GREEN_LEVELS = tuple(round(value * 63 / 255) * 255 // 63 for value in range(256))


@dataclass(frozen=True)
class RowBinding:
    """Named horizontal support, with immutable influences on the existing rig."""

    name: str
    local_y: float
    source_y: float
    left_weights: tuple[tuple[str, float], ...]
    right_weights: tuple[tuple[str, float], ...]
    vertical_weights: tuple[tuple[str, float], ...]


@dataclass(frozen=True)
class RowMapping:
    """Source row and destination span; no triangle rasterization is involved."""

    source_y: float
    destination_y: float
    left: float
    right: float


# image: RGB or RGBA pixels to quantize while preserving any alpha channel.
# Raises ValueError for an unsupported image mode.
def native_texture_colors(image):
    if image.mode not in ('RGB', 'RGBA'):
        raise ValueError(f'Jelly texture requires RGB or RGBA, got {image.mode}')
    channels = list(image.split())
    channels[0] = channels[0].point(RED_BLUE_LEVELS)
    channels[1] = channels[1].point(GREEN_LEVELS)
    channels[2] = channels[2].point(RED_BLUE_LEVELS)
    return Image.merge(image.mode, channels)


# weights: Normalized named influences prepared once for a row binding.
# offsets: Current displacement of each semantic lobe relative to the rest pose.
# Raises KeyError for a missing anchor rather than substituting a motion value.
def weighted_offset(weights, offsets):
    horizontal = 0.0
    vertical = 0.0
    for name, weight in weights:
        horizontal += offsets[name][0] * weight
        vertical += offsets[name][1] * weight
    return horizontal, vertical


class JellyScanlineStudy:
    """One decoded sprite, 33 rig-bound row supports and a single sample pass."""

    # self: New isolated simulator-only study instance.
    # root: Checkout owning the face-free transparent jelly artwork.
    # rig: Validated cloud authoring contract with center and contour-lobe anchors.
    # Raises OSError for an unreadable PNG and ValueError for incompatible artwork/rig.
    def __init__(self, root, rig):
        if rig['character']['id'] != 'cody' or rig['root_point'] != 'center':
            raise ValueError('The pearlescent jelly sprite is authored for the Cody cloud')
        self.rig = rig
        path = root / 'assets/cody/appearance/jelly-scanline/jelly-body-master.png'
        with Image.open(path) as source:
            if source.mode != 'RGBA' or source.getchannel('A').getextrema() != (0, 255):
                raise ValueError(f'Jelly artwork needs genuine transparent and opaque RGBA pixels: {path}')
            bounds = source.getchannel('A').point(lambda alpha: 255 if alpha >= 64 else 0).getbbox()
            if bounds is None:
                raise ValueError(f'Jelly artwork has no visible body: {path}')
            left, top, right, bottom = bounds
            self.source_crop = (max(0, left - 4), max(0, top - 4),
                                min(source.width, right + 4), min(source.height, bottom + 4))
            cropped = source.crop(self.source_crop)
            prepared_height = round(320 * cropped.height / cropped.width)
            prepared = cropped.convert('RGBa').resize((320, prepared_height), Image.Resampling.LANCZOS)
            self.texture = native_texture_colors(prepared.convert('RGBA'))
        self.premultiplied_texture = self.texture.convert('RGBa')

        transform = tuple(rig['default_transform'][key] for key in ('x', 'y', 'scale'))
        rest = evaluate(rig, rig['boot_channels']['ready'], transform)
        center_x, center_y = rest['world']['center']
        scale = rest['scale']
        self.anchors = {'center': (0.0, 0.0)}
        for lobe in rig['contour']['lobes']:
            name = lobe['point']
            horizontal, vertical = rest['world'][name]
            self.anchors[name] = ((horizontal - center_x) / scale, (vertical - center_y) / scale)
        outline = rest['rest_outline']
        self.local_left = (min(horizontal for horizontal, _ in outline) - center_x) / scale
        self.local_right = (max(horizontal for horizontal, _ in outline) - center_x) / scale
        local_top = (min(vertical for _, vertical in outline) - center_y) / scale
        local_bottom = (max(vertical for _, vertical in outline) - center_y) / scale
        self.rows = []
        for row in range(33):
            fraction = row / 32
            local_y = local_top + (local_bottom - local_top) * fraction
            self.rows.append(RowBinding(
                name=f'jelly.row.{row:02d}', local_y=local_y, source_y=fraction * self.texture.height,
                left_weights=bind_weights((self.local_left, local_y), self.anchors),
                right_weights=bind_weights((self.local_right, local_y), self.anchors),
                vertical_weights=bind_weights((0.0, local_y), self.anchors),
            ))

    # self: Immutable row bindings, texture geometry and rest anchors.
    # scene: Evaluated pose already shared by face, root motion and contour controls.
    # Raises ValueError for non-finite, collapsed or reversed row mappings.
    # Raises KeyError if a required semantic rig anchor is absent.
    def map_rows(self, scene):
        center_x, center_y = scene['world']['center']
        scale = scene['scale']
        offsets = {}
        for name, (rest_x, rest_y) in self.anchors.items():
            horizontal, vertical = scene['world'][name]
            offsets[name] = (horizontal - center_x - rest_x * scale,
                             vertical - center_y - rest_y * scale)
        mappings = []
        for binding in self.rows:
            left_offset, _ = weighted_offset(binding.left_weights, offsets)
            right_offset, _ = weighted_offset(binding.right_weights, offsets)
            _, vertical_offset = weighted_offset(binding.vertical_weights, offsets)
            mapping = RowMapping(
                source_y=binding.source_y,
                destination_y=center_y + binding.local_y * scale + vertical_offset,
                left=center_x + self.local_left * scale + left_offset,
                right=center_x + self.local_right * scale + right_offset,
            )
            if not all(math.isfinite(value) for value in (mapping.destination_y, mapping.left, mapping.right)):
                raise ValueError(f'Non-finite jelly row: {binding.name}')
            if mapping.right - mapping.left <= .001:
                raise ValueError(f'Collapsed jelly row width: {binding.name}')
            if mappings and mapping.destination_y - mappings[-1].destination_y <= .001:
                raise ValueError(f'Inverted jelly row order: {binding.name}')
            mappings.append(mapping)
        return mappings

    # self: Immutable local rectangle and source support rows.
    # scene: Unwarped rig pose sharing its root and breathing scale with the sprite.
    # mappings: Destination controls prepared once from that exact pose.
    # point: Unwarped logical facial position.
    # Raises ValueError for non-finite or excessive mapped geometry.
    # Raises KeyError if the canonical center is missing.
    def warp_point(self, scene, mappings, point):
        center_x, center_y = scene['world']['center']
        local_y = (point[1] - center_y) / scene['scale']
        support = 0
        while support + 1 < len(self.rows) - 1 and local_y >= self.rows[support + 1].local_y:
            support += 1
        source_upper, source_lower = self.rows[support], self.rows[support + 1]
        upper, lower = mappings[support], mappings[support + 1]
        fraction = (local_y - source_upper.local_y) / (source_lower.local_y - source_upper.local_y)
        left = upper.left + (lower.left - upper.left) * fraction
        right = upper.right + (lower.right - upper.right) * fraction
        local_x = (point[0] - center_x) / scene['scale']
        horizontal = (local_x - self.local_left) / (self.local_right - self.local_left)
        warped = (left + (right - left) * horizontal,
                  upper.destination_y + (lower.destination_y - upper.destination_y) * fraction)
        if not all(math.isfinite(value) and abs(value) <= 2048 for value in warped):
            raise ValueError(f'Invalid jelly face mapping: {point} -> {warped}')
        return warped

    # self: Validated authoring contract and the same sprite bindings used by paint.
    # scene: Fresh transient rig pose, modified exactly once before drawing or touch dispatch.
    # Raises ValueError for a rejected mapping; KeyError for missing authored identities.
    def deform_face(self, scene):
        mappings = self.map_rows(scene)
        body_paths = {self.rig['contour']['path'], *self.rig['contour'].get('surface_paths', [])}
        facial_anchors = {
            entity['anchor'] for entity in self.rig['entities'].values()
            if not any(path in body_paths for path in entity['paths'])
        }
        definitions = {point['name']: point for point in self.rig['points']}
        for path in scene['paths']:
            if path['definition']['name'] not in body_paths:
                # Warp already sampled cubics, not just their control points.
                path['vertices'] = [self.warp_point(scene, mappings, point) for point in path['vertices']]
        for name, point in tuple(scene['world'].items()):
            ancestor = name
            while ancestor is not None and ancestor not in facial_anchors:
                ancestor = definitions[ancestor]['parent']
            if ancestor is not None:
                scene['world'][name] = self.warp_point(scene, mappings, point)

    # self: Prepared sprite and row mappings; the master PNG is never decoded here.
    # image: RGB logical canvas receiving each visible row exactly once.
    # scene: Shared evaluated rig pose; no sensor reads or fabricated measurements.
    # Raises ValueError for an incompatible canvas or a folded row mapping.
    # Raises KeyError if a required semantic anchor is absent.
    def paint(self, image, scene):
        if image.mode != 'RGB':
            raise ValueError('Jelly scanline study requires an RGB destination')
        mappings = self.map_rows(scene)
        first_row = max(0, math.floor(mappings[0].destination_y))
        last_row = min(image.height, math.ceil(mappings[-1].destination_y))
        support = 0
        for row in range(first_row, last_row):
            sample_y = row + .5
            if sample_y < mappings[0].destination_y or sample_y >= mappings[-1].destination_y:
                continue
            while support + 1 < len(mappings) - 1 and sample_y >= mappings[support + 1].destination_y:
                support += 1
            upper, lower = mappings[support], mappings[support + 1]
            row_height = lower.destination_y - upper.destination_y
            fraction = (sample_y - upper.destination_y) / row_height
            left = upper.left + (lower.left - upper.left) * fraction
            right = upper.right + (lower.right - upper.right) * fraction
            first_column = max(0, math.floor(left))
            last_column = min(image.width, math.ceil(right))
            if first_column >= last_column:
                continue
            source_y = upper.source_y + (lower.source_y - upper.source_y) * fraction
            source_y_step = (lower.source_y - upper.source_y) / row_height
            source_x_step = self.texture.width / (right - left)
            coefficients = (source_x_step, 0.0, (first_column - left) * source_x_step,
                            0.0, source_y_step, source_y - source_y_step * .5)
            # Pillow executes this inverse affine row sample on the host. A device
            # port uses its own fixed-point sampler; host speed proves nothing
            # about ESP32 timing. Premultiplied filtering keeps alpha edges clean.
            strip = self.premultiplied_texture.transform(
                (last_column - first_column, 1), Image.Transform.AFFINE,
                coefficients, resample=Image.Resampling.BILINEAR,
            ).convert('RGBA')
            image.paste(strip, (first_column, row), strip)
        image.paste(native_texture_colors(image))
