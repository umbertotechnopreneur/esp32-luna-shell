"""Host-only cloud material study; this is not the firmware rendering contract."""

from __future__ import annotations

import math

from cody_coverage import FULL_COVERAGE, coverage_mask


# Each volume is tied to a semantic rig anchor. Broad overlapping supports reach
# the actual silhouette; they are not isolated oval decals pasted onto the face.
# Values are center offsets, horizontal/vertical radii, and height, in body units.
CLOUD_VOLUMES = {
    "face": ("center", -.015, .045, .66, .55, .44),
    "crown": ("lobe.crown", -.03, .22, .52, .39, .43),
    "upper_left": ("lobe.upper_left", -.16, .16, .44, .37, .36),
    "upper_right": ("lobe.upper_right", .14, .13, .31, .26, .31),
    "shoulder": ("lobe.shoulder", .10, .08, .30, .29, .28),
    "left": ("lobe.left", .20, .12, .33, .30, .29),
    "right": ("lobe.right", -.18, .10, .36, .27, .30),
    "lower_right": ("lobe.lower_right", .10, -.01, .34, .25, .27),
    "right_foot": ("lobe.foot_right", -.03, -.06, .24, .22, .23),
    "belly": ("lobe.belly", .19, .015, .40, .23, .27),
    "left_foot": ("lobe.foot_left", .02, -.10, .24, .25, .22),
    "lower_left": ("lobe.lower_left", -.13, -.04, .22, .20, .20),
}


# Secondary billows sculpt the same surface rather than paint separate circles.
# Their names are material identities; tuple positions are only the compact data encoding.
# Values are anchor, center offsets, radii and relief height in body units.
CLOUD_BILLOWS = {
    "crown.left": ("lobe.crown", -.20, .11, .20, .17, .10),
    "crown.summit": ("lobe.crown", .015, .055, .20, .16, .105),
    "crown.right": ("lobe.crown", .23, .18, .17, .18, .09),
    "crown.front_left": ("lobe.crown", -.27, .29, .19, .19, .12),
    "crown.front": ("lobe.crown", -.035, .32, .22, .18, .10),
    "crown.front_right": ("lobe.crown", .19, .38, .16, .15, .085),
    "left.shoulder": ("lobe.upper_left", -.15, .10, .22, .17, .12),
    "left.shoulder_front": ("lobe.upper_left", .05, .24, .19, .17, .105),
    "left.outer_top": ("lobe.left", .16, -.16, .18, .20, .10),
    "left.outer_middle": ("lobe.left", .085, .035, .19, .18, .13),
    "left.inner": ("lobe.left", .32, .19, .20, .17, .12),
    "left.outer_bottom": ("lobe.left", .12, .25, .18, .16, .10),
    "right.shoulder": ("lobe.upper_right", .13, .10, .17, .14, .10),
    "right.shoulder_front": ("lobe.shoulder", -.03, .05, .19, .17, .105),
    "right.outer_top": ("lobe.right", -.15, -.12, .20, .17, .11),
    "right.outer_middle": ("lobe.right", -.02, .05, .15, .16, .095),
    "right.inner": ("lobe.right", -.30, .17, .19, .18, .12),
    "right.outer_bottom": ("lobe.lower_right", .09, -.035, .21, .17, .12),
    "base.left_outer": ("lobe.lower_left", -.15, -.04, .17, .17, .115),
    "base.left_foot": ("lobe.foot_left", .025, -.075, .19, .16, .12),
    "base.left_front": ("center", -.34, .38, .19, .15, .13),
    "base.middle_left": ("center", -.10, .46, .18, .15, .12),
    "base.middle_right": ("center", .14, .43, .18, .17, .12),
    "base.right_front": ("center", .37, .43, .20, .16, .125),
    "base.right_foot": ("lobe.foot_right", -.015, -.10, .19, .18, .105),
}


# horizontal: Integer material-space coordinate; texture moves with the character.
# vertical: Integer material-space coordinate in the same body-relative grid.
# salt: Distinguishes the fine pigment variation from quantization dither.
def material_noise(horizontal, vertical, salt=0):
    value = (horizontal * 374761393 + vertical * 668265263 + salt * 1442695041) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((value ^ (value >> 16)) & 65535) / 65536


# horizontal: Continuous body-relative texture coordinate.
# vertical: Continuous coordinate in the same grid.
# wavelength: Positive size of a softly interpolated pigment cell.
# salt: Stable identity for this texture octave.
def soft_material_noise(horizontal, vertical, wavelength, salt):
    cell_x = math.floor(horizontal / wavelength)
    cell_y = math.floor(vertical / wavelength)
    blend_x = horizontal / wavelength - cell_x
    blend_y = vertical / wavelength - cell_y
    blend_x = blend_x * blend_x * (3 - 2 * blend_x)
    blend_y = blend_y * blend_y * (3 - 2 * blend_y)
    upper = (1 - blend_x) * material_noise(cell_x, cell_y, salt)
    upper += blend_x * material_noise(cell_x + 1, cell_y, salt)
    lower = (1 - blend_x) * material_noise(cell_x, cell_y + 1, salt)
    lower += blend_x * material_noise(cell_x + 1, cell_y + 1, salt)
    return upper * (1 - blend_y) + lower * blend_y


# values: Floating point sculpted heights; never quantized before taking normals.
# width: Logical number of columns.
# height: Logical number of rows.
def soften_height_field(values, width, height):
    horizontal = [0.0] * len(values)
    result = [0.0] * len(values)
    for row in range(height):
        offset = row * width
        for column in range(width):
            left = values[offset + max(0, column - 1)]
            right = values[offset + min(width - 1, column + 1)]
            horizontal[offset + column] = (left + 2 * values[offset + column] + right) * .25
    for row in range(height):
        upper = max(0, row - 1) * width
        lower = min(height - 1, row + 1) * width
        offset = row * width
        for column in range(width):
            result[offset + column] = (
                horizontal[upper + column] + 2 * horizontal[offset + column]
                + horizontal[lower + column]
            ) * .25
    return result


# first: Current continuous surface height.
# second: Height of the overlapping spherical billow.
# radius: Small positive join width in height units, not a screen-space blur radius.
def merge_billow_height(first, second, radius):
    overlap = max(0, radius - abs(first - second)) / radius
    return max(first, second) + overlap * overlap * radius * .25


# heights: Continuous destination relief modified in place.
# foundation: Frozen parent surface used to embed this generation of billows.
# size: Logical canvas dimensions.
# center: Billow center in logical pixel coordinates.
# radii: Horizontal and vertical support radii in pixels.
# rise: Positive height of the sphere before embedding.
# join_width: Soft union width in height units.
def sculpt_billow(heights, foundation, size, center, radii, rise, join_width):
    width, height = size
    center_x, center_y = center
    radius_x, radius_y = radii
    center_column = min(width - 1, max(0, round(center_x)))
    center_row = min(height - 1, max(0, round(center_y)))
    foundation_height = foundation[center_row * width + center_column] - rise * .58
    left = max(0, math.floor(center_x - radius_x))
    right = min(width, math.ceil(center_x + radius_x))
    top = max(0, math.floor(center_y - radius_y))
    bottom = min(height, math.ceil(center_y + radius_y))
    for row in range(top, bottom):
        vertical = (row + .5 - center_y) / radius_y
        for column in range(left, right):
            horizontal = (column + .5 - center_x) / radius_x
            support = 1 - horizontal * horizontal - vertical * vertical
            if support <= 0:
                continue
            index = row * width + column
            surface_height = foundation_height + rise * math.sqrt(support)
            heights[index] = merge_billow_height(heights[index], surface_height, join_width)


# scene: Evaluated semantic anchors and breathing scale shared with normal rendering.
# size: Destination width and height in logical canvas pixels.
# Raises KeyError for a missing semantic anchor in an unsupported skin.
def cloud_height_field(scene, size):
    width, height = size
    scale = scene['scale']
    heights = [0.0] * (width * height)
    for anchor, offset_x, offset_y, radius_x, radius_y, rise in CLOUD_VOLUMES.values():
        anchor_x, anchor_y = scene['world'][anchor]
        center_x = anchor_x + offset_x * scale
        center_y = anchor_y + offset_y * scale
        radius_x *= scale
        radius_y *= scale
        rise *= scale
        left = max(0, math.floor(center_x - radius_x))
        right = min(width, math.ceil(center_x + radius_x))
        top = max(0, math.floor(center_y - radius_y))
        bottom = min(height, math.ceil(center_y + radius_y))
        for row in range(top, bottom):
            vertical = (row + .5 - center_y) / radius_y
            for column in range(left, right):
                horizontal = (column + .5 - center_x) / radius_x
                squared_radius = horizontal * horizontal + vertical * vertical
                if squared_radius >= 1:
                    continue
                index = row * width + column
                surface_height = rise * math.sqrt(1 - squared_radius)
                heights[index] = merge_billow_height(heights[index], surface_height, scale * .027)

    heights = soften_height_field(heights, width, height)
    foundation = list(heights)
    placed_billows = {}
    for identity, values in CLOUD_BILLOWS.items():
        anchor, offset_x, offset_y, radius_x, radius_y, rise = values
        anchor_x, anchor_y = scene['world'][anchor]
        center_x = anchor_x + offset_x * scale
        center_y = anchor_y + offset_y * scale
        radius_x *= scale
        radius_y *= scale
        rise *= scale * 1.35
        placed_billows[identity] = (center_x, center_y, radius_x, radius_y, rise)
        sculpt_billow(heights, foundation, size, (center_x, center_y),
                      (radius_x, radius_y), rise, scale * .035)

    # Small irregular caps interrupt the perfectly smooth spheres. Their parent
    # surface is frozen so detail placement cannot amplify a previously added cap.
    foundation = list(heights)
    curls = {
        'upper': (-.32, -.33, .47, .45),
        'side': (.37, -.08, .43, .40),
        'lower': (-.03, .36, .42, .37),
    }
    for identity, values in placed_billows.items():
        center_x, center_y, radius_x, radius_y, rise = values
        seed = sum((position + 1) * ord(character) for position, character in enumerate(identity))
        angle = (material_noise(seed, 0, 23) - .5) * 1.8
        cosine, sine = math.cos(angle), math.sin(angle)
        for curl_name, (offset_x, offset_y, width_ratio, height_ratio) in curls.items():
            curl_seed = sum(ord(character) for character in curl_name)
            variation = .85 + material_noise(seed, curl_seed, 29) * .30
            curl_x = center_x + (offset_x * cosine - offset_y * sine) * radius_x
            curl_y = center_y + (offset_x * sine + offset_y * cosine) * radius_y
            sculpt_billow(heights, foundation, size, (curl_x, curl_y),
                          (radius_x * width_ratio * variation, radius_y * height_ratio * variation),
                          rise * .36, scale * .022)

    center_x, center_y = scene['world']['center']
    material_scale = 145 / scale
    for row in range(height):
        for column in range(width):
            index = row * width + column
            if heights[index] <= 0:
                continue
            local_x = (column - center_x) * material_scale
            local_y = (row - center_y) * material_scale
            # The face needs a quiet surface; fine relief belongs to the cloud
            # lobes, not to wrinkles crossing the eyes and mouth.
            face_distance = (local_x / 56) ** 2 + ((local_y + 4) / 45) ** 2
            relief_weight = 1 - .90 * math.exp(-face_distance * 1.4)
            texture = (soft_material_noise(local_x, local_y, 6, 31) - .5) * 1.1
            texture += (soft_material_noise(local_x, local_y, 2.5, 37) - .5) * .35
            heights[index] += texture * relief_weight / material_scale

    # Keep precision through sculpting and lighting. An 8-bit height map creates
    # concentric contour lines in the normal field, even when its colors are dithered.
    return soften_height_field(heights, width, height)


# red: Continuous red byte before native RGB565 quantization.
# green: Continuous green byte before native RGB565 quantization.
# blue: Continuous blue byte before native RGB565 quantization.
# threshold: Deterministic stochastic-rounding threshold in [0, 1).
def native_color(red, green, blue, threshold):
    channels = []
    for value, maximum in ((red, 31), (green, 63), (blue, 31)):
        native = min(maximum, max(0, math.floor(value * maximum / 255 + threshold)))
        channels.append(native)
    return (channels[0] << 11) | (channels[1] << 5) | channels[2]


# foreground: Quantized native RGB565 material color.
# background: Existing RGB destination pixel below the body.
# coverage: Full-precision geometric boundary coverage in [0, 1024].
def covered_color(foreground, background, coverage):
    red, green, blue = background
    destination = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
    opacity = (255 * coverage + FULL_COVERAGE // 2) // FULL_COVERAGE
    result = []
    for shift, maximum in ((11, 31), (5, 63), (0, 31)):
        source_channel = (foreground >> shift) & maximum
        below_channel = (destination >> shift) & maximum
        native = (source_channel * opacity + below_channel * (255 - opacity) + 127) // 255
        result.append(native * 255 // maximum)
    return tuple(result)


# image: RGB logical destination; the body is composited once using its original coverage mask.
# vertices: Existing evaluated body outline, without changing silhouette or touch geometry.
# scene: Evaluated semantic anchors and breathing scale for all local cloud volumes.
# Raises ValueError for a non-RGB target or invalid coverage geometry.
# Raises KeyError for a missing semantic anchor in an unsupported skin.
def paint_cloud_material(image, vertices, scene):
    if image.mode != 'RGB':
        raise ValueError('The host-only cloud material requires an RGB destination')
    mask = coverage_mask(vertices, image.size)
    bounds = mask.getbbox()
    if bounds is None:
        return
    heights = cloud_height_field(scene, image.size)
    coverage = mask.load()
    pixels = image.load()
    width, height = image.size
    center_x, center_y = scene['world']['center']
    material_scale = 145 / scene['scale']
    shadow = (102, 134, 174)
    for row in range(bounds[1], bounds[3]):
        warmth = max(0, min(1, (center_y + 65 - row) / 165))
        sunlight = (229 + 26 * warmth, 239 + 7 * warmth, 247 - 28 * warmth)
        underside = max(0, min(1, (row - center_y + 8) / 100))
        for column in range(bounds[0], bounds[2]):
            amount = coverage[column, row]
            if amount == 0:
                continue
            offset = row * width + column
            surface_height = heights[offset]
            left = heights[row * width + max(0, column - 1)]
            right = heights[row * width + min(width - 1, column + 1)]
            above = heights[max(0, row - 1) * width + column]
            below = heights[min(height - 1, row + 1) * width + column]
            normal_x = -(right - left) * .5
            normal_y = -(below - above) * .5
            length = math.sqrt(normal_x * normal_x + normal_y * normal_y + 1)
            normal_x /= length
            normal_y /= length
            normal_z = 1 / length
            diffuse = max(0, -.40 * normal_x - .57 * normal_y + .72 * normal_z)
            occlusion = 0.0
            for distance in (5, 12):
                for direction_x, direction_y in ((-1, 0), (1, 0), (0, -1), (0, 1),
                                                 (-.707, -.707), (.707, -.707),
                                                 (-.707, .707), (.707, .707)):
                    neighbor_x = min(width - 1, max(0, column + round(direction_x * distance)))
                    neighbor_y = min(height - 1, max(0, row + round(direction_y * distance)))
                    higher = heights[neighbor_y * width + neighbor_x] - surface_height
                    occlusion += max(0, higher / distance - .22)
            occlusion = min(.20, occlusion * .019)
            cast_shadow = 0.0
            for distance in (4, 8, 14, 22, 32):
                light_x = max(0, column - round(distance * .57))
                light_y = max(0, row - round(distance * .82))
                obstruction = heights[light_y * width + light_x] - surface_height - distance * 1.03
                cast_shadow = max(cast_shadow, min(1, max(0, obstruction / 5)))
            illumination = max(0, min(1, .20 + .78 * diffuse + .10 * normal_z
                                       - occlusion - cast_shadow * .16 - underside * .07))
            local_x = math.floor((column - center_x) * material_scale)
            local_y = math.floor((row - center_y) * material_scale)
            pigment = (soft_material_noise(local_x, local_y, 9, 3) - .5) * 3.2
            pigment += (soft_material_noise(local_x, local_y, 3, 5) - .5) * 1.8
            pigment += (material_noise(local_x, local_y, 7) - .5) * 1.2
            color = [shade + (light - shade) * illumination + pigment
                     for shade, light in zip(shadow, sunlight)]
            # Dither is part of this visual study, not invented firmware telemetry.
            # Native quantization is retained while long horizontal bands dissolve.
            native = native_color(*color, material_noise(local_x, local_y, 19))
            pixels[column, row] = covered_color(native, pixels[column, row], amount)
