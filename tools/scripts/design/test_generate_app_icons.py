"""Unit tests for the application icon geometry.

The icon artwork is committed, so these tests guard the geometry that produced
it: if a constant drifts, the numbers below fail rather than the next
regeneration silently emitting different artwork. Pillow is not needed, because
the generator only imports it inside its rendering helpers.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ElementTree

MODULE_PATH = Path(__file__).with_name("generate_app_icons.py")
SPEC = importlib.util.spec_from_file_location("design_generate_app_icons", MODULE_PATH)
assert SPEC and SPEC.loader
icons = importlib.util.module_from_spec(SPEC)
# The module defines dataclasses, which resolve their annotations through
# sys.modules while the module body executes.
sys.modules[SPEC.name] = icons
SPEC.loader.exec_module(icons)


class StaffGeometryTests(unittest.TestCase):
    def test_each_system_carries_four_rules(self) -> None:
        """`repeating-linear-gradient` fits four 2px rules into a 96px system."""
        self.assertEqual(icons.staff_line_offsets(), (0, 24, 48, 72))

    def test_the_staff_block_is_centred_vertically(self) -> None:
        tops = icons.system_tops()
        self.assertEqual(tops, (372.0, 556.0))
        bottom = tops[-1] + icons.SYSTEM_HEIGHT
        self.assertAlmostEqual(tops[0], icons.CANVAS - bottom)

    def test_rules_span_the_full_canvas_width(self) -> None:
        rectangles = icons.staff_line_rectangles()
        self.assertEqual(len(rectangles), 8)
        for left, top, right, bottom in rectangles:
            self.assertEqual((left, right), (0.0, float(icons.CANVAS)))
            self.assertEqual(bottom - top, icons.STAFF_LINE_THICKNESS)


class NoteGeometryTests(unittest.TestCase):
    def test_every_inline_offset_becomes_a_notehead(self) -> None:
        centres = icons.note_centres()
        self.assertEqual(len(centres), sum(len(row) for row in icons.NOTE_OFFSETS))
        self.assertEqual(centres[0], (137.0, 444.0))

    def test_notes_past_the_right_edge_are_kept_for_clipping(self) -> None:
        """The source relies on `overflow: hidden` rather than trimming the row."""
        self.assertTrue(any(centre[0] > icons.CANVAS for centre in icons.note_centres()))

    def test_the_notehead_polygon_matches_the_rotated_ellipse(self) -> None:
        centre = (500.0, 500.0)
        polygon = icons.note_polygon(centre)
        self.assertEqual(len(polygon), icons.NOTE_SEGMENTS)
        # A rotation about the centre preserves every point's distance from it,
        # so the extreme radii still come from the unrotated ellipse.
        radii = [((x - centre[0]) ** 2 + (y - centre[1]) ** 2) ** 0.5 for x, y in polygon]
        self.assertAlmostEqual(max(radii), icons.NOTE_WIDTH / 2)
        self.assertAlmostEqual(min(radii), icons.NOTE_HEIGHT / 2)

    def test_the_stem_stays_axis_aligned(self) -> None:
        """The notehead's -20deg and the stem's own +20deg cancel out."""
        centre = icons.note_centres()[0]
        left, top, right, bottom = icons.stem_rectangle(centre)
        self.assertAlmostEqual(right - left, icons.STEM_WIDTH)
        self.assertAlmostEqual(bottom - top, icons.STEM_HEIGHT)

    def test_the_stem_rises_from_the_notehead(self) -> None:
        centre = icons.note_centres()[0]
        _, _, _, bottom = icons.stem_rectangle(centre)
        self.assertLess(bottom, centre[1])
        self.assertGreater(bottom, centre[1] - icons.NOTE_HEIGHT)

    def test_the_staff_layer_holds_every_shape(self) -> None:
        notes = len(icons.note_centres())
        expected = len(icons.staff_line_rectangles()) + 2 * notes
        self.assertEqual(len(icons.staff_polygons()), expected)


class MonogramTests(unittest.TestCase):
    def test_the_element_box_includes_the_trailing_letter_space(self) -> None:
        self.assertEqual(icons.mark_box(), (432.0, 320.0))

    def test_the_ink_is_centred_on_the_canvas(self) -> None:
        points = [point for contour in icons.mark_contours() for s in contour for point in s[1:]]
        xs = [point[0] for point in points]
        ys = [point[1] for point in points]
        self.assertAlmostEqual((min(xs) + max(xs)) / 2, icons.CANVAS / 2)
        self.assertAlmostEqual((min(ys) + max(ys)) / 2, icons.CANVAS / 2)

    def test_the_monogram_keeps_its_counter(self) -> None:
        """P contributes an outer contour and a bowl; T contributes one."""
        self.assertEqual(len(icons.mark_contours()), 3)

    def test_flattening_walks_from_the_starting_point(self) -> None:
        contour = icons.mark_contours()[0]
        polygon = icons.flatten_contour(contour)
        self.assertEqual(polygon[0], contour[0][1])
        self.assertEqual(polygon[-1], contour[-1][-1])
        self.assertGreater(len(polygon), len(contour))

    def test_glyph_advances_are_monospaced(self) -> None:
        for character, outlines in icons.GLYPH_OUTLINES.items():
            self.assertTrue(outlines, f"{character} has no outline")
        self.assertEqual(len(icons.GLYPH_OUTLINES), len(set(icons.MARK_TEXT)))


class GradientTests(unittest.TestCase):
    def test_the_gradient_line_is_centred_and_correctly_angled(self) -> None:
        start, end = icons.gradient_line()
        self.assertAlmostEqual((start[0] + end[0]) / 2, icons.CANVAS / 2)
        self.assertAlmostEqual((start[1] + end[1]) / 2, icons.CANVAS / 2)
        # 160deg runs mostly downwards and slightly to the right.
        self.assertGreater(end[1], start[1])
        self.assertGreater(end[0], start[0])

    def test_the_gradient_line_length_matches_the_css_definition(self) -> None:
        import math

        start, end = icons.gradient_line()
        width, height = icons.mark_box()
        radians = math.radians(icons.GRADIENT_ANGLE_DEGREES)
        expected = abs(width * math.sin(radians)) + abs(height * math.cos(radians))
        length = math.dist(start, end)
        self.assertAlmostEqual(length, expected)

    def test_offsets_run_from_zero_to_one_and_clamp(self) -> None:
        start, end = icons.gradient_line()
        self.assertAlmostEqual(icons.gradient_offset(start), 0.0)
        self.assertAlmostEqual(icons.gradient_offset(end), 1.0)
        self.assertAlmostEqual(icons.gradient_offset((icons.CANVAS / 2,) * 2), 0.5)
        self.assertEqual(icons.gradient_offset((0.0, 0.0)), 0.0)
        self.assertEqual(icons.gradient_offset((float(icons.CANVAS),) * 2), 1.0)

    def test_stop_colours_are_reproduced_exactly(self) -> None:
        for offset, colour in icons.DARK.gradient:
            self.assertEqual(icons.gradient_colour(icons.DARK.gradient, offset),
                             icons.parse_colour(colour))

    def test_colours_interpolate_between_stops(self) -> None:
        halfway = icons.gradient_colour(icons.DARK.gradient, 0.275)
        first = icons.parse_colour("#EEF1F7")
        second = icons.parse_colour("#9EC9FF")
        for channel, low, high in zip(halfway, first, second):
            self.assertAlmostEqual(channel, (low + high) / 2, delta=1)

    def test_a_malformed_colour_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            icons.parse_colour("#abc")


class SvgTests(unittest.TestCase):
    def test_the_document_is_well_formed_and_square(self) -> None:
        root = ElementTree.fromstring(icons.build_svg(icons.DARK))
        self.assertEqual(root.get("viewBox"), f"0 0 {icons.CANVAS} {icons.CANVAS}")

    def test_the_dark_variant_carries_the_glow_filter(self) -> None:
        document = icons.build_svg(icons.DARK)
        self.assertIn("feGaussianBlur", document)
        self.assertIn(f'stdDeviation="{icons.GLOW_BLUR_RADIUS / 2:g}"', document)

    def test_the_light_variant_has_no_glow(self) -> None:
        """`ICO-light.html` drops the text shadow entirely."""
        self.assertIsNone(icons.LIGHT.glow)
        self.assertNotIn("feGaussianBlur", icons.build_svg(icons.LIGHT))

    def test_every_contour_is_closed(self) -> None:
        self.assertEqual(icons.mark_path_data().count("Z"), len(icons.mark_contours()))
        self.assertTrue(icons.mark_path_data().startswith("M"))

    def test_variants_do_not_share_element_identifiers(self) -> None:
        """Both documents can be inlined into one page without colliding."""

        def identifiers(palette) -> set[str]:
            root = ElementTree.fromstring(icons.build_svg(palette))
            return {element.get("id") for element in root.iter() if element.get("id")}

        dark = identifiers(icons.DARK)
        self.assertTrue(dark)
        self.assertFalse(dark & identifiers(icons.LIGHT))


class OutputTests(unittest.TestCase):
    def test_png_sizes_are_descending_powers_the_packaging_installs(self) -> None:
        self.assertEqual(icons.ICON_SIZES[0], icons.CANVAS)
        self.assertEqual(list(icons.ICON_SIZES), sorted(icons.ICON_SIZES, reverse=True))

    def test_windows_icons_stay_within_the_format_limit(self) -> None:
        self.assertLessEqual(max(icons.ICO_SIZES), 256)

    def test_committed_artwork_covers_every_declared_size(self) -> None:
        directory = icons.OUTPUT_DIRECTORY
        if not directory.is_dir():
            self.skipTest(f"{directory} is not present")
        expected = [f"{icons.DARK.name}-{size}.png" for size in icons.ICON_SIZES]
        expected += [f"{icons.LIGHT.name}-{size}.png" for size in icons.LIGHT_ICON_SIZES]
        expected += [f"{icons.DARK.name}.svg", f"{icons.LIGHT.name}.svg", f"{icons.DARK.name}.ico"]
        for name in expected:
            self.assertTrue((directory / name).is_file(), f"missing {name}")


if __name__ == "__main__":
    unittest.main()
