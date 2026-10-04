"""Bounded polygon coverage matching the standalone firmware's four-sample fill."""

from __future__ import annotations

import math

from PIL import Image, ImageDraw


VERTICAL_SAMPLES = 4
HORIZONTAL_UNITS = 256
FULL_COVERAGE = VERTICAL_SAMPLES * HORIZONTAL_UNITS


# differences: Row-local changes with one sentinel beyond the visible width.
# left: Clipped span start measured in 1/256-pixel units.
# right: Exclusive clipped span end, strictly greater than left.
def add_coverage_span(differences, left, right):
    first_pixel = left // HORIZONTAL_UNITS
    last_pixel = (right - 1) // HORIZONTAL_UNITS
    if first_pixel == last_pixel:
        length = right - left
        differences[first_pixel] += length
        differences[first_pixel + 1] -= length
        return

    first_coverage = HORIZONTAL_UNITS - left % HORIZONTAL_UNITS
    last_coverage = right - last_pixel * HORIZONTAL_UNITS
    # Four changes preserve fully covered interiors as runs, just as on device.
    differences[first_pixel] += first_coverage
    differences[first_pixel + 1] += HORIZONTAL_UNITS - first_coverage
    differences[last_pixel] += last_coverage - HORIZONTAL_UNITS
    differences[last_pixel + 1] -= last_coverage


# subject: Sorted subject polygon crossing pairs at one exact subscanline.
# containing: Sorted clip crossing pairs, or None for an ordinary fill.
def intersect_spans(subject, containing):
    if containing is None:
        for hit in range(0, len(subject), 2):
            yield subject[hit], subject[hit + 1]
        return
    subject_hit = 0
    clip_hit = 0
    while subject_hit < len(subject) and clip_hit < len(containing):
        subject_end = subject[subject_hit + 1]
        clip_end = containing[clip_hit + 1]
        left = max(subject[subject_hit], containing[clip_hit])
        right = min(subject_end, clip_end)
        if subject_end <= clip_end:
            subject_hit += 2
        if clip_end <= subject_end:
            clip_hit += 2
        if left < right:
            yield left, right


# vertices: Evaluated logical polygon, optionally repeating its first vertex for closure.
# size: Positive logical mask width and height, bounded by the 466-pixel canvas.
# clip_vertices: Optional containing polygon intersected before coverage accumulation.
# Raises ValueError for invalid geometry, an odd crossing count or impossible coverage.
def coverage_mask(vertices, size, clip_vertices=None):
    if len(size) != 2 or any(type(value) is not int or not 0 < value <= 466 for value in size):
        raise ValueError(f"Invalid coverage mask size: {size!r}")
    if not 3 <= len(vertices) <= 256:
        raise ValueError(f"Invalid coverage vertex count: {len(vertices)}")
    if clip_vertices is not None and (len(clip_vertices) < 3 or len(vertices) + len(clip_vertices) > 256):
        raise ValueError("Clipped coverage exceeds the shared native intersection capacity")
    checked_polygons = (vertices,) if clip_vertices is None else (vertices, clip_vertices)
    for point in (point for polygon in checked_polygons for point in polygon):
        if len(point) != 2 or any(
            type(value) not in (float, int) or not math.isfinite(value) or abs(value) > 2048
            for value in point
        ):
            raise ValueError(f"Invalid coverage point: {point!r}")

    width, height = size
    mask = Image.new("I", size, 0)
    top = min(point[1] for point in vertices)
    bottom = max(point[1] for point in vertices)
    if top == bottom or bottom <= 0 or top >= height:
        return mask

    first_sample = .5 / VERTICAL_SAMPLES
    last_sample = height - first_sample
    edges = []
    for index, start in enumerate(vertices):
        end = vertices[(index + 1) % len(vertices)]
        if min(start[1], end[1]) <= last_sample and max(start[1], end[1]) > first_sample:
            edges.append((start, end))
    clip_edges = []
    if clip_vertices is not None:
        for index, start in enumerate(clip_vertices):
            end = clip_vertices[(index + 1) % len(clip_vertices)]
            if min(start[1], end[1]) <= last_sample and max(start[1], end[1]) > first_sample:
                clip_edges.append((start, end))

    draw = ImageDraw.Draw(mask)
    for row in range(max(0, math.floor(top)), min(height, math.ceil(bottom))):
        differences = [0] * (width + 1)
        first_covered = width
        past_last_covered = 0
        for sample in range(VERTICAL_SAMPLES):
            scan_y = row + (sample + .5) / VERTICAL_SAMPLES
            intersections = []
            for start, end in edges:
                if (start[1] <= scan_y < end[1]) or (end[1] <= scan_y < start[1]):
                    crossing = start[0] + (scan_y - start[1]) * (end[0] - start[0]) / (end[1] - start[1])
                    intersections.append(crossing)
            if len(intersections) % 2:
                raise ValueError(f"Coverage row {row}, sample {sample}: odd crossing count {len(intersections)}")
            intersections.sort()
            clip_intersections = None
            if clip_vertices is not None:
                clip_intersections = []
                for start, end in clip_edges:
                    if (start[1] <= scan_y < end[1]) or (end[1] <= scan_y < start[1]):
                        crossing = start[0] + (scan_y - start[1]) * (end[0] - start[0]) / (end[1] - start[1])
                        clip_intersections.append(crossing)
                if len(clip_intersections) % 2:
                    raise ValueError(f"Coverage clip row {row}, sample {sample}: odd crossing count")
                clip_intersections.sort()
            # Intersect geometry at each sample; multiplying final alpha masks
            # would double-round boundaries and disagree with the native fill.
            for span_left, span_right in intersect_spans(intersections, clip_intersections):
                left = max(0, span_left)
                right = min(width, span_right)
                if left >= right:
                    continue
                fixed_left = math.floor(left * HORIZONTAL_UNITS + .5)
                fixed_right = math.floor(right * HORIZONTAL_UNITS + .5)
                if fixed_left == fixed_right:
                    continue
                add_coverage_span(differences, fixed_left, fixed_right)
                first_covered = min(first_covered, fixed_left // HORIZONTAL_UNITS)
                past_last_covered = max(
                    past_last_covered,
                    (fixed_right + HORIZONTAL_UNITS - 1) // HORIZONTAL_UNITS,
                )

        column = first_covered
        coverage = 0
        while column < past_last_covered:
            coverage += differences[column]
            if not 0 <= coverage <= FULL_COVERAGE:
                raise ValueError(f"Coverage row {row}, column {column}: rejected value {coverage}")
            past_run = column + 1
            while past_run < past_last_covered and differences[past_run] == 0:
                past_run += 1
            if coverage:
                draw.line((column, row, past_run - 1, row), fill=coverage)
            column = past_run
    return mask
