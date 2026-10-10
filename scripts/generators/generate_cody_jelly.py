# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Package the approved Cody skin catalog and shared row bindings as flash data."""

from pathlib import Path
import hashlib
import json
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'simulator/windows'))

from scripts.common.cody_rig_contract import load_contract, enum_identifier
from scripts.common.asset_provenance import text_source_hash
from cody_jelly_scanline import JellyScanlineStudy, native_texture_colors

OUTPUT = ROOT / 'assets/cody/appearance/jelly-scanline/generated'
CATALOG = ROOT / 'assets/cody/appearance/jelly-scanline/skins.json'


# value: Finite coordinate or normalized binding weight from the loaded contract.
def cpp_float(value):
    return f'{value:.9f}f'


# weights: Named normalized influences for one horizontal or vertical row control.
# names: Explicit anchor order shared by generated identities and storage.
# Raises ValueError if a duplicate, missing or ambiguous reference is found.
def weight_array(weights, names):
    values = dict(weights)
    if len(values) != len(weights) or set(values) != set(names):
        raise ValueError('Jelly row has duplicate or mismatched anchor references')
    return '{' + ', '.join(cpp_float(values[name]) for name in names) + '}'


# No parameters: load and validate authored skin identities, names and source paths.
# Raises OSError for catalog I/O and ValueError for malformed or ambiguous identities.
def load_skin_catalog():
    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    if catalog.get('version') != 1 or not isinstance(catalog.get('skins'), list):
        raise ValueError('Jelly skin catalog version or skin list is invalid')
    skins = catalog['skins']
    if not skins:
        raise ValueError('Jelly skin catalog must contain at least one skin')
    required = {'id', 'enum_value', 'cpp_id', 'symbol', 'display_name', 'source'}
    for skin in skins:
        if set(skin) != required:
            raise ValueError(f'Jelly skin entry has an invalid schema: {skin!r}')
        if not all(isinstance(skin[field], str) for field in required - {'enum_value'}):
            raise ValueError(f'Jelly skin entry has a non-text field: {skin!r}')
        if not isinstance(skin['enum_value'], int) or not 0 < skin['enum_value'] <= 65535:
            raise ValueError(f"Jelly skin enum value is invalid: {skin['enum_value']!r}")
        if enum_identifier(skin['cpp_id']) != skin['cpp_id']:
            raise ValueError(f"Jelly skin C++ identity is invalid: {skin['cpp_id']!r}")
        if not skin['symbol'].isascii() or not skin['symbol'].isidentifier():
            raise ValueError(f"Jelly skin symbol is invalid: {skin['symbol']!r}")
    for field in ('id', 'enum_value', 'cpp_id', 'symbol', 'display_name', 'source'):
        values = [skin[field] for skin in skins]
        if len(set(values)) != len(values):
            raise ValueError(f'Jelly skin catalog has duplicate {field}')
    default_id = catalog.get('default_id')
    if not isinstance(default_id, str) or sum(skin['id'] == default_id for skin in skins) != 1:
        raise ValueError('Jelly skin default_id must reference exactly one authored skin')
    for skin in skins:
        source = (ROOT / skin['source']).resolve()
        try:
            source.relative_to(ROOT.resolve())
        except ValueError as error:
            raise ValueError(f"Jelly skin source escapes the repository: {skin['source']}") from error
        if not source.is_file():
            raise ValueError(f"Jelly skin source is missing: {skin['source']}")
    return catalog


# source_path: Authored transparent skin with the canonical master geometry.
# source_crop: Canonical crop shared by every skin; no per-skin geometry is inferred.
# output_size: Firmware texture dimensions generated from the canonical master.
# canonical_size: Full source dimensions required before applying the shared crop.
# canonical_alpha: Exact full-canvas alpha bytes required for identical silhouette geometry.
# Raises OSError for source I/O and ValueError for geometry or alpha mismatches.
def prepare_skin(source_path, source_crop, output_size, canonical_size, canonical_alpha):
    with Image.open(source_path) as source:
        if source.mode != 'RGBA' or source.size != canonical_size:
            raise ValueError(f'Jelly skin must be canonical RGBA {canonical_size}: {source_path}')
        alpha = source.getchannel('A').tobytes()
        if alpha != canonical_alpha:
            raise ValueError(f'Jelly skin alpha geometry differs from the canonical master: {source_path}')
        prepared = source.crop(source_crop).convert('RGBa').resize(output_size, Image.Resampling.LANCZOS)
        texture = native_texture_colors(prepared.convert('RGBA')).convert('RGBa')
        return texture.tobytes()


# source_bytes: Packed premultiplied RGBA8 texture bytes for one authored skin.
# symbol: Unique generated C++ storage symbol without namespace qualification.
# Raises ValueError when source storage is not a complete sequence of RGBA words.
def emit_texture(source_bytes, symbol):
    if len(source_bytes) % 4 != 0:
        raise ValueError(f'Jelly texture {symbol} has an incomplete RGBA word')
    words = [int.from_bytes(source_bytes[offset:offset + 4], 'little')
             for offset in range(0, len(source_bytes), 4)]
    source = [f'alignas(4) const uint32_t pixels_{symbol}[width * height] = {{']
    for offset in range(0, len(words), 12):
        source.append('    ' + ', '.join(f'0x{word:08x}U' for word in words[offset:offset + 12]) + ',')
    source.append('};')
    return source


# prepared_skins: Validated RGBA8 textures, including every selectable skin.
# width: Number of packed texels in each generated row.
# height: Number of generated rows.
# Raises ValueError if a texture has an unexpected byte count.
def row_coverage(prepared_skins, width, height):
    bounds = []
    for row in range(height):
        first = width
        last = 0
        for _, _, texture in prepared_skins:
            if len(texture) != width * height * 4:
                raise ValueError('Jelly texture has an unexpected byte count')
            row_start = row * width * 4
            for column in range(width):
                offset = row_start + column * 4
                if any(texture[offset:offset + 4]):
                    first = min(first, column)
                    last = max(last, column + 1)
        bounds.append((first, last))
    return bounds


# No parameters: emit versioned source assets, never firmware configuration or tests.
# Raises OSError for asset I/O and ValueError for invalid authoring or identity data.
def main():
    rig, _ = load_contract(ROOT)
    study = JellyScanlineStudy(ROOT, rig)
    catalog = load_skin_catalog()
    skins = catalog['skins']
    names = list(study.anchors)
    row_names = [row.name for row in study.rows]
    if len(set(row_names)) != len(row_names):
        raise ValueError('Duplicate jelly control-row identity')
    row_ids = [enum_identifier(name) for name in row_names]
    if len(set(row_ids)) != len(row_ids):
        raise ValueError('Ambiguous jelly C++ row identity')
    width, height = study.texture.size
    master_path = ROOT / 'assets/cody/appearance/jelly-scanline/jelly-body-master.png'
    with Image.open(master_path) as master:
        if master.mode != 'RGBA':
            raise ValueError(f'Canonical jelly master must be RGBA: {master_path}')
        canonical_size = master.size
        canonical_alpha = master.getchannel('A').tobytes()
    alpha_sha256 = hashlib.sha256(canonical_alpha).hexdigest()
    prepared_skins = []
    for skin in skins:
        path = ROOT / skin['source']
        texture_bytes = prepare_skin(
            path, study.source_crop, (width, height), canonical_size, canonical_alpha
        )
        prepared_skins.append((skin, path, texture_bytes))

    skin_header = [
        '// Generated by scripts/device.ps1 generate -Generator cody-jelly.',
        '#pragma once', '#include <cstdint>',
        'namespace CODY {',
        'enum class JellySkinId : uint16_t {',
    ]
    for skin in skins:
        skin_header.append(f"    {skin['cpp_id']} = {skin['enum_value']},")
    skin_header.extend([
        '};',
        'struct JellySkinDefinition {',
        '    JellySkinId id;',
        '    const char *key;',
        '    const char *display_name;',
        '};',
        'namespace JellySkinCatalog {',
        'inline constexpr JellySkinDefinition definitions[] = {',
    ])
    for skin in skins:
        skin_header.append(
            f"    {{JellySkinId::{skin['cpp_id']}, \"{skin['id']}\", \"{skin['display_name']}\"}},"
        )
    default_skin = next(skin for skin in skins if skin['id'] == catalog['default_id'])
    skin_header.extend([
        '};',
        f"inline constexpr JellySkinId default_skin = JellySkinId::{default_skin['cpp_id']};",
        '}  // namespace JellySkinCatalog',
        '}  // namespace CODY',
    ])

    header = [
        '// Generated by scripts/device.ps1 generate -Generator cody-jelly.',
        '#pragma once', '#include <array>', '#include "cody_jelly_skins.hpp"',
        '#include "cloud_rig_data.hpp"',
        'namespace CODY::JellyData {',
        f'inline constexpr uint16_t width = {width};',
        f'inline constexpr uint16_t height = {height};',
        f'inline constexpr float local_left = {cpp_float(study.local_left)};',
        f'inline constexpr float local_right = {cpp_float(study.local_right)};',
    ]
    for skin in skins:
        header.append(f"extern const uint32_t pixels_{skin['symbol']}[width * height];")
    header.extend([
        'const uint32_t *pixels_for(JellySkinId identity);',
        'struct RowCoverage { uint16_t first; uint16_t last; };',
        'extern const RowCoverage row_coverage[height];',
        'enum class RowId : uint8_t { ' + ', '.join(row_ids) + ', Count };',
        'struct Anchor { PointId point; CODY2D::Vec2 rest; };',
        'inline constexpr Anchor anchors[] = {',
    ])
    for name, (horizontal, vertical) in study.anchors.items():
        header.append('    {PointId::' + enum_identifier(name) + ', {' +
                      cpp_float(horizontal) + ', ' + cpp_float(vertical) + '}},')
    header.extend([
        '};', f'using Weights = std::array<float, {len(names)}>;',
        'struct RowBinding {',
        '    RowId id;', '    float local_y;', '    float source_y;',
        '    Weights left;', '    Weights right;', '    Weights vertical;', '};',
        'inline constexpr RowBinding rows[] = {',
    ])
    for row, identity in zip(study.rows, row_ids):
        fields = [f'RowId::{identity}', cpp_float(row.local_y), cpp_float(row.source_y),
                  weight_array(row.left_weights, names), weight_array(row.right_weights, names),
                  weight_array(row.vertical_weights, names)]
        header.append('    {' + ', '.join(fields) + '},')
    header.extend(['};', '}'])

    source = ['// Generated jelly skin catalog; do not edit packed pixels.',
              '#include "cody_jelly_data.hpp"', 'namespace CODY::JellyData {']
    for skin, _, source_bytes in prepared_skins:
        source.extend(emit_texture(source_bytes, skin['symbol']))
    source.append('const RowCoverage row_coverage[height] = {')
    for first, last in row_coverage(prepared_skins, width, height):
        source.append(f'    {{{first}, {last}}},')
    source.append('};')
    source.extend(['const uint32_t *pixels_for(JellySkinId identity)', '{', '    switch (identity) {'])
    for skin in skins:
        source.extend([
            f"    case JellySkinId::{skin['cpp_id']}:",
            f"        return pixels_{skin['symbol']};",
        ])
    source.extend(['    default:', '        return nullptr;', '    }', '}', '}'])

    total_texture_bytes = sum(len(source_bytes) for _, _, source_bytes in prepared_skins)
    manifest = {
        'catalog': CATALOG.relative_to(ROOT).as_posix(),
        'catalog_sha256': text_source_hash(CATALOG),
        'default_id': catalog['default_id'],
        'alpha_sha256': alpha_sha256,
        'skin_sha256': text_source_hash(ROOT / 'assets/cody/cody.yaml'),
        'source_crop': study.source_crop, 'width': width, 'height': height,
        'encoding': 'premultiplied RGBA8 in little-endian uint32, RGB565-quantized source',
        'texture_bytes_each': width * height * 4,
        'texture_bytes_total': total_texture_bytes,
        'skins': [
            {
                'id': skin['id'], 'enum_value': skin['enum_value'], 'cpp_id': skin['cpp_id'],
                'display_name': skin['display_name'], 'source': path.relative_to(ROOT).as_posix(),
                'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'texture_sha256': hashlib.sha256(source_bytes).hexdigest(),
            }
            for skin, path, source_bytes in prepared_skins
        ],
        'row_controls': len(study.rows),
        'anchors': names, 'note': 'Asset generation only; not a device performance measurement.',
    }
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / 'cody_jelly_skins.hpp').write_text('\n'.join(skin_header) + '\n', encoding='utf-8')
    (OUTPUT / 'cody_jelly_data.hpp').write_text('\n'.join(header) + '\n', encoding='utf-8')
    (OUTPUT / 'cody_jelly_data.cpp').write_text('\n'.join(source) + '\n', encoding='utf-8')
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Jelly catalog packaged: {len(skins)} skins, {width}x{height}, '
          f'{total_texture_bytes} flash bytes, {len(study.rows)} controls')


if __name__ == '__main__':
    main()
