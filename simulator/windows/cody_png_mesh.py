"""Opt-in PNG/triangle experiment. CPU software preview, not firmware parity."""

from __future__ import annotations

from dataclasses import dataclass
import math

from PIL import Image, ImageDraw

from scripts.common.cody_rig_contract import evaluate


@dataclass(frozen=True)
class MeshVertex:
    """A stable mesh identity, immutable UV, rest point and named rig influences."""

    name: str
    local: tuple[float, float]
    uv: tuple[float, float]
    weights: tuple[tuple[str, float], ...]


@dataclass(frozen=True)
class MeshTriangle:
    """Vertex references use names; traversal order does not give them meaning."""

    name: str
    vertices: tuple[str, str, str]


# point: Local mesh vertex to attach to the existing semantic rig.
# anchors: Named undeformed center/lobe coordinates from the authored skin.
def bind_weights(point, anchors):
    influences = []
    for name, position in anchors.items():
        distance_squared = (point[0] - position[0]) ** 2 + (point[1] - position[1]) ** 2
        # The root keeps the facial region quiet. Nearby lobes influence both
        # the painted shadow and the alpha silhouette through these same weights.
        strength = 1.8 if name == 'center' else 1.0
        weight = strength / (distance_squared + .035) ** 2
        influences.append((name, weight))
    total = sum(weight for _, weight in influences)
    return tuple((name, weight / total) for name, weight in influences)


# start: First corner of the directed edge.
# end: Second corner of the directed edge.
# point: Position to classify in the triangle plane.
def edge_value(start, end, point):
    return (end[0] - start[0]) * (point[1] - start[1]) - (end[1] - start[1]) * (point[0] - start[0])


class PngMeshStudy:
    """Decode once, bind once, then sample the same painted texture per frame."""

    # self: New host-only mesh instance.
    # root: Checkout containing the versioned PNG master.
    # rig: Validated Cody authoring contract, including semantic lobe identities.
    # Raises OSError for unreadable artwork and ValueError for incompatible artwork/rig.
    def __init__(self, root, rig):
        if rig['character']['id'] != 'cody' or rig['root_point'] != 'center':
            raise ValueError('The experimental PNG is authored specifically for the Cody cloud skin')
        path = root / 'assets/cody/experiments/png-mesh/cloud-body-master.png'
        with Image.open(path) as source:
            if source.mode != 'RGBA':
                raise ValueError(f'PNG mesh artwork requires genuine RGBA transparency: {path}')
            alpha = source.getchannel('A')
            if alpha.getextrema() != (0, 255):
                raise ValueError(f'PNG mesh artwork must include transparent and opaque pixels: {path}')
            # Placement ignores extremely faint peripheral pigment; padding keeps
            # the visible alpha feather. The versioned master itself is never edited.
            bounds = alpha.point(lambda value: 255 if value >= 64 else 0).getbbox()
            if bounds is None:
                raise ValueError(f'PNG mesh artwork has no visible cloud: {path}')
            left, top, right, bottom = bounds
            crop = (max(0, left - 4), max(0, top - 4),
                    min(source.width, right + 4), min(source.height, bottom + 4))
            cropped = source.crop(crop)
            texture_height = round(320 * cropped.height / cropped.width)
            # Premultiplication before filtering avoids a dark halo around the cutout.
            self.texture = cropped.convert('RGBa').resize(
                (320, texture_height), Image.Resampling.LANCZOS).convert('RGBA')
            self.source_crop = crop

        # This study uses RGB565 source colors plus alpha8, not the old 32-color ramp.
        red, green, blue, alpha = self.texture.split()
        red = red.point([round(value * 31 / 255) * 255 // 31 for value in range(256)])
        green = green.point([round(value * 63 / 255) * 255 // 63 for value in range(256)])
        blue = blue.point([round(value * 31 / 255) * 255 // 31 for value in range(256)])
        self.texture = Image.merge('RGBA', (red, green, blue, alpha))
        self.premultiplied_texture = self.texture.convert('RGBa')
        self.texels = self.premultiplied_texture.load()
        self.vertices = {}
        self.triangles = []
        transform = tuple(rig['default_transform'][key] for key in ('x', 'y', 'scale'))
        rest_scene = evaluate(rig, rig['boot_channels']['ready'], transform)
        center_x, center_y = rest_scene['world']['center']
        scale = rest_scene['scale']
        self.anchors = {'center': (0.0, 0.0)}
        for lobe in rig['contour']['lobes']:
            name = lobe['point']
            anchor_x, anchor_y = rest_scene['world'][name]
            self.anchors[name] = ((anchor_x - center_x) / scale, (anchor_y - center_y) / scale)
        outline = rest_scene['rest_outline']
        bounds = ((min(x for x, _ in outline) - center_x) / scale,
                  (min(y for _, y in outline) - center_y) / scale,
                  (max(x for x, _ in outline) - center_x) / scale,
                  (max(y for _, y in outline) - center_y) / scale)
        self.build_mesh(bounds)

    # self: Mesh instance with a decoded texture and semantic rest anchors.
    # bounds: Local left, top, right and bottom of the undeformed cloud envelope.
    def build_mesh(self, bounds):
        left, top, right, bottom = bounds
        columns, rows = 11, 9
        names = {}
        for row in range(rows):
            vertical = row / (rows - 1)
            for column in range(columns):
                horizontal = column / (columns - 1)
                name = f'mesh.vertex.r{row:02d}.c{column:02d}'
                point = (left + horizontal * (right - left), top + vertical * (bottom - top))
                uv = (horizontal * (self.texture.width - 1), vertical * (self.texture.height - 1))
                self.vertices[name] = MeshVertex(name, point, uv, bind_weights(point, self.anchors))
                names[row, column] = name
        alpha = self.texture.getchannel('A')
        for row in range(rows - 1):
            for column in range(columns - 1):
                upper_left = names[row, column]
                upper_right = names[row, column + 1]
                lower_left = names[row + 1, column]
                lower_right = names[row + 1, column + 1]
                first_uv = self.vertices[upper_left].uv
                last_uv = self.vertices[lower_right].uv
                support = (math.floor(first_uv[0]), math.floor(first_uv[1]),
                           math.ceil(last_uv[0]) + 1, math.ceil(last_uv[1]) + 1)
                if alpha.crop(support).getbbox() is None:
                    continue
                cell = f'mesh.triangle.r{row:02d}.c{column:02d}'
                self.triangles.append(MeshTriangle(cell + '.upper', (upper_left, upper_right, lower_right)))
                self.triangles.append(MeshTriangle(cell + '.lower', (upper_left, lower_right, lower_left)))

    # self: Mesh instance whose binding and UVs never change during motion.
    # scene: Same evaluated pose used by the ordinary facial renderer.
    # Raises KeyError for a missing rig anchor rather than substituting a pose.
    def deform(self, scene):
        center_x, center_y = scene['world']['center']
        scale = scene['scale']
        offsets = {}
        for name, (rest_x, rest_y) in self.anchors.items():
            world_x, world_y = scene['world'][name]
            offsets[name] = (world_x - center_x - rest_x * scale,
                             world_y - center_y - rest_y * scale)
        positions = {}
        for name, vertex in self.vertices.items():
            horizontal = center_x + vertex.local[0] * scale
            vertical = center_y + vertex.local[1] * scale
            for anchor, weight in vertex.weights:
                horizontal += offsets[anchor][0] * weight
                vertical += offsets[anchor][1] * weight
            positions[name] = (horizontal, vertical)
        return positions

    # self: Cached premultiplied texture, sampled without another PNG decode.
    # horizontal: Continuous source coordinate obtained by inverse triangle mapping.
    # vertical: Continuous source coordinate obtained by inverse triangle mapping.
    def sample(self, horizontal, vertical):
        horizontal = min(self.texture.width - 1, max(0.0, horizontal))
        vertical = min(self.texture.height - 1, max(0.0, vertical))
        left, top = math.floor(horizontal), math.floor(vertical)
        right = min(self.texture.width - 1, left + 1)
        bottom = min(self.texture.height - 1, top + 1)
        blend_x, blend_y = horizontal - left, vertical - top
        weights = ((1 - blend_x) * (1 - blend_y), blend_x * (1 - blend_y),
                   (1 - blend_x) * blend_y, blend_x * blend_y)
        texels = (self.texels[left, top], self.texels[right, top],
                  self.texels[left, bottom], self.texels[right, bottom])
        channels = [sum(texel[channel] * weight for texel, weight in zip(texels, weights))
                    for channel in range(4)]
        alpha = channels[3]
        if alpha < .5:
            return (0, 0, 0, 0)
        return tuple(min(255, round(value * 255 / alpha)) for value in channels[:3]) + (round(alpha),)

    # self: Immutable source UVs and premultiplied texels for the study.
    # layer: Transparent full-frame target; composition happens only after every triangle.
    # triangle: Named vertex references with positive signed area in screen coordinates.
    # positions: Deformed screen positions indexed by the same stable mesh names.
    # Raises ValueError for a collapsed or flipped triangle; never hides a folded mesh.
    def raster_triangle(self, layer, triangle, positions):
        first, second, third = (positions[name] for name in triangle.vertices)
        area = edge_value(first, second, third)
        if not math.isfinite(area) or area <= .001:
            raise ValueError(f'PNG mesh triangle collapsed or inverted: {triangle.name}, area={area}')
        uv_first, uv_second, uv_third = (self.vertices[name].uv for name in triangle.vertices)
        left = max(0, math.floor(min(first[0], second[0], third[0])))
        right = min(layer.width, math.ceil(max(first[0], second[0], third[0])))
        top = max(0, math.floor(min(first[1], second[1], third[1])))
        bottom = min(layer.height, math.ceil(max(first[1], second[1], third[1])))
        pixels = layer.load()
        for row in range(top, bottom):
            for column in range(left, right):
                point = (column + .5, row + .5)
                weight_first = edge_value(second, third, point) / area
                weight_second = edge_value(third, first, point) / area
                weight_third = 1 - weight_first - weight_second
                if min(weight_first, weight_second, weight_third) < -1e-9:
                    continue
                source_x = weight_first * uv_first[0] + weight_second * uv_second[0] + weight_third * uv_third[0]
                source_y = weight_first * uv_first[1] + weight_second * uv_second[1] + weight_third * uv_third[1]
                # Adjacent triangles agree on their shared UV edge. They may overwrite
                # that edge, but never alpha-blend it twice and create a dark seam.
                pixels[column, row] = self.sample(source_x, source_y)

    # self: Cached mesh, texture and skin binding.
    # image: RGB logical canvas; no separate vector outline clips the warped alpha.
    # scene: Current evaluated rig pose, shared with the face.
    # overlay: Draws the actual triangle network for inspection, not an invented skeleton.
    # Raises ValueError for an incompatible canvas or a folded triangle.
    def paint(self, image, scene, overlay=False):
        if image.mode != 'RGB':
            raise ValueError('PNG mesh study requires an RGB canvas')
        positions = self.deform(scene)
        layer = Image.new('RGBA', image.size)
        for triangle in self.triangles:
            self.raster_triangle(layer, triangle, positions)
        image.paste(layer, (0, 0), layer)
        # Quantize the displayed result too; this is still host math, not an ESP32
        # timing claim or a claim of byte-for-byte native raster parity.
        red, green, blue = image.split()
        red = red.point([round(value * 31 / 255) * 255 // 31 for value in range(256)])
        green = green.point([round(value * 63 / 255) * 255 // 63 for value in range(256)])
        blue = blue.point([round(value * 31 / 255) * 255 // 31 for value in range(256)])
        image.paste(Image.merge('RGB', (red, green, blue)))
        if overlay:
            draw = ImageDraw.Draw(image)
            for triangle in self.triangles:
                points = [positions[name] for name in triangle.vertices]
                draw.line(points + points[:1], fill='#DC9477', width=1)
            for name in self.anchors:
                horizontal, vertical = scene['world'][name]
                draw.ellipse((horizontal - 2, vertical - 2, horizontal + 2, vertical + 2), fill='#6AEBEC')
