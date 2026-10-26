# Builds the sneaker of Photo mode's "Sneaker" scene in Blender, bakes
# its ambient occlusion and the shade it casts on the floor, and writes
# what the app reads (data/photo):
#
#   sneaker.bin       the shoe's surface (millimetres, y up, the toe along +x)
#   sneaker.json      its eyelets and the guide for the lacing
#   sneaker_floor.png the floor's view of the room past the shoe
#
#   blender -b -P make_sneaker.py -- <data/photo folder> [preview folder]
#
# A canvas shoe on a rubber sole, made from measurements (EU 43, 290 mm
# long): the upper is a dome over the footprint whose sides rise steeply
# from the sole and round over into the top; the opening for the foot and
# the slot for the tongue are cut out of it, the eyelets are punched
# through the flaps on both sides of the slot.

import bpy
import bmesh
import json
import math
import os
import struct
import sys

import numpy as np
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

OUT = argv[0] if len(argv) >= 1 else os.path.dirname(os.path.abspath(__file__))
PREVIEW = argv[1] if len(argv) >= 2 else ""

# --------------------------------------------------------------------------
# Measurements (millimetres; Blender: x heel -> toe, y across, z up)
# --------------------------------------------------------------------------

SOLE = 28.0          # height of the sole
LIP = 2.6            # the sole stands out this much around the upper
SHELL = 2.2          # thickness of the canvas (with its lining)
LACE = 4.5           # diameter of the lace
HOLE = 2.7           # radius of an eyelet's hole
GAP = SHELL + LACE + 0.6   # the tongue lies this far below the top of the flaps

ROWS = 6             # pairs of eyelets
ROW_X = [208.0 - 14.0 * k for k in range(ROWS)]   # toe -> collar

# The footprint (outsole), counter-clockwise from the back of the heel.
# Right shoe: the big toe's side (+y) bulges at the ball, the arch (+y,
# x 90..150) is drawn in.
FOOTPRINT = [
    (0.0, 0.0), (3.0, -20.0), (14.0, -32.0), (38.0, -37.0), (80.0, -36.0),
    (125.0, -38.0), (170.0, -45.0), (205.0, -49.0), (238.0, -46.0),
    (263.0, -37.0), (281.0, -22.0), (290.0, -3.0), (288.0, 14.0),
    (277.0, 32.0), (252.0, 46.0), (216.0, 51.0), (178.0, 45.0),
    (138.0, 34.0), (100.0, 32.0), (60.0, 35.0), (24.0, 34.0), (6.0, 22.0),
]


def catmull_rom_closed(points, samples):
    """A smooth closed curve through the points (centripetal Catmull-Rom),
    evenly spaced along its length."""
    p = np.array(points, dtype=float)
    n = len(p)

    dense = []

    for i in range(n):
        p0, p1, p2, p3 = p[(i - 1) % n], p[i], p[(i + 1) % n], p[(i + 2) % n]

        def tj(ti, a, b):
            return ti + math.sqrt(np.linalg.norm(b - a)) + 1e-9

        t0 = 0.0
        t1 = tj(t0, p0, p1)
        t2 = tj(t1, p1, p2)
        t3 = tj(t2, p2, p3)

        for t in np.linspace(t1, t2, 40, endpoint=False):
            a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1
            a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2
            a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3
            b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2
            b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3
            dense.append((t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2)

    dense = np.array(dense)

    # evenly along the length
    seg = np.linalg.norm(np.roll(dense, -1, axis=0) - dense, axis=1)
    s = np.concatenate([[0.0], np.cumsum(seg)])
    total = s[-1]
    closed = np.vstack([dense, dense[:1]])

    targets = np.linspace(0.0, total, samples, endpoint=False)
    x = np.interp(targets, s, closed[:, 0])
    y = np.interp(targets, s, closed[:, 1])

    return np.stack([x, y], axis=1)


def inward_normals(curve):
    """Unit normals pointing into a counter-clockwise closed curve."""
    t = np.roll(curve, -1, axis=0) - np.roll(curve, 1, axis=0)
    t /= np.linalg.norm(t, axis=1)[:, None]

    return np.stack([-t[:, 1], t[:, 0]], axis=1)


def distance_to_curve(points, curve):
    """Distance from each point to the closed polyline."""
    a = curve
    b = np.roll(curve, -1, axis=0)
    ab = b - a
    ab2 = np.maximum((ab * ab).sum(axis=1), 1e-12)

    result = np.full(len(points), 1e9)

    for start in range(0, len(points), 2048):
        q = points[start:start + 2048][:, None, :]
        t = np.clip(((q - a[None]) * ab[None]).sum(axis=2) / ab2[None], 0.0, 1.0)
        closest = a[None] + t[..., None] * ab[None]
        d = np.linalg.norm(q - closest, axis=2).min(axis=1)
        result[start:start + 2048] = d

    return result


def inside(points, polygon):
    """Even-odd test of points against a closed polygon."""
    x = points[:, 0][:, None]
    y = points[:, 1][:, None]
    ax = polygon[:, 0][None]
    ay = polygon[:, 1][None]
    bx = np.roll(polygon[:, 0], -1)[None]
    by = np.roll(polygon[:, 1], -1)[None]

    crosses = ((ay > y) != (by > y)) & (x < (bx - ax) * (y - ay) / (by - ay + 1e-12) + ax)

    return crosses.sum(axis=1) % 2 == 1


OUTSOLE = catmull_rom_closed(FOOTPRINT, 480)
UPPER = OUTSOLE + inward_normals(OUTSOLE) * LIP

# star-shaped about this point: every ray from it meets the outline once
CENTRE = np.array([148.0, 1.0])


def spring(x):
    """The toe lifts off the floor (toe spring)."""
    return 9.0 * np.clip((np.asarray(x) - 215.0) / 75.0, 0.0, None) ** 2


def table(x, xs, ys):
    return np.interp(np.asarray(x, dtype=float), xs, ys)


# height of the top of the upper above the sole, along the shoe
CREST_X = [0.0, 30.0, 80.0, 120.0, 150.0, 185.0, 215.0, 245.0, 270.0, 290.0]
CREST_Z = [50.0, 54.0, 70.0, 78.0, 70.0, 58.0, 47.0, 37.0, 29.0, 22.0]

# how far in from the edge the side rounds over into the top
ROUND_X = [0.0, 40.0, 120.0, 200.0, 260.0, 290.0]
ROUND_R = [16.0, 18.0, 24.0, 34.0, 32.0, 28.0]


def dome(xy):
    """Height of the upper's outer surface over the footprint."""
    x = xy[:, 0]
    d = distance_to_curve(xy, UPPER)

    r = table(x, ROUND_X, ROUND_R)
    q = np.clip(d / r, 0.0, 1.0)
    profile = np.sqrt(1.0 - (1.0 - q) ** 2)

    # a little lower towards the sides (the top is rounded across)
    across = np.clip(np.abs(xy[:, 1] - 0.0) / 50.0, 0.0, 1.0)
    crest = table(x, CREST_X, CREST_Z) * (1.0 - 0.10 * across ** 2)

    return SOLE + spring(x) + crest * profile


# The opening for the foot and the slot of the tongue, seen from above.
def collar_outline():
    pts = []

    # a rounded opening from the heel to the front of the ankle
    for a in np.linspace(0.0, 2.0 * math.pi, 120, endpoint=False):
        cx, cy = 84.0, 0.0
        rx, ry = 57.0, 28.0
        x = cx + rx * math.cos(a)
        y = cy + ry * math.sin(a) * (1.0 - 0.12 * math.cos(a))
        pts.append((x, y))

    return np.array(pts)


def slot_halfwidth(x):
    # wider at the collar, where the flaps are pulled apart less
    return table(x, [128.0, 150.0, 212.0, 220.0], [12.0, 11.0, 7.5, 4.0])


def slot_outline():
    xs = np.linspace(128.0, 214.0, 60)
    w = slot_halfwidth(xs)

    right = np.stack([xs, -w], axis=1)
    left = np.stack([xs[::-1], w[::-1]], axis=1)

    # a rounded end at the toe side
    end = []

    for a in np.linspace(-math.pi / 2, math.pi / 2, 12)[1:-1]:
        end.append((214.0 + 4.0 * math.cos(a), 4.0 * math.sin(a)))

    return np.vstack([right, np.array(end), left])


COLLAR = collar_outline()
SLOT = slot_outline()


def eyelet_positions():
    """Centres of the eyelets on the flaps (x, y), toe -> collar, the
    left side (+y) first in each row."""
    rows = []

    for x in ROW_X:
        w = float(slot_halfwidth(x)) + 7.0
        rows.append(((x, w), (x, -w)))

    return rows


EYELETS = eyelet_positions()


# --------------------------------------------------------------------------
# Building the objects
# --------------------------------------------------------------------------

def new_object(name, mesh):
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)

    return obj


def mesh_from(name, verts, faces):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    mesh.update()

    for p in mesh.polygons:
        p.use_smooth = True

    return mesh


def material(name, color, roughness, metallic=0.0, sheen=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True

    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic

    if "Sheen Weight" in bsdf.inputs:
        bsdf.inputs["Sheen Weight"].default_value = sheen

    return mat


# app material numbers (the shoe shader in knotglwidget.cpp)
CANVAS, RUBBER, METAL, LINING, TOECAP, TONGUE = 0, 1, 2, 3, 4, 5

MATERIALS = [
    material("canvas", (0.035, 0.055, 0.11), 0.85, sheen=0.4),
    material("rubber", (0.80, 0.79, 0.75), 0.55),
    material("metal", (0.80, 0.80, 0.78), 0.3, metallic=1.0),
    material("lining", (0.55, 0.53, 0.50), 0.9),
    material("toecap", (0.82, 0.81, 0.77), 0.5),
    material("tongue", (0.035, 0.055, 0.11), 0.85, sheen=0.4),
]


def assign(obj, index):
    while len(obj.data.materials) < len(MATERIALS):
        obj.data.materials.append(MATERIALS[len(obj.data.materials)])

    for p in obj.data.polygons:
        p.material_index = index


def apply_modifiers(obj):
    bpy.context.view_layer.objects.active = obj

    for m in list(obj.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def boolean(obj, cutter, operation="DIFFERENCE"):
    m = obj.modifiers.new("cut", "BOOLEAN")
    m.object = cutter
    m.operation = operation
    m.solver = "EXACT"

    apply_modifiers(obj)


def prism(name, polygon, z0, z1):
    n = len(polygon)
    verts = [(x, y, z0) for x, y in polygon] + [(x, y, z1) for x, y in polygon]
    faces = [list(range(n))[::-1], list(range(n, 2 * n))]

    for i in range(n):
        j = (i + 1) % n
        faces.append([i, j, n + j, n + i])

    obj = new_object(name, mesh_from(name, verts, faces))
    obj.hide_render = True

    return obj


def remove(obj):
    bpy.data.objects.remove(obj, do_unlink=True)


def dome_surface(name):
    """The upper's outer surface: rings of the outline drawn in towards the
    middle, raised to the dome."""
    rings = 70
    n = len(UPPER)

    # rings close together near the edge (the steep sides)
    rho = 1.0 - (np.arange(rings) / rings) ** 1.6

    verts = []

    for r in rho:
        for p in UPPER:
            verts.append(CENTRE + r * (p - CENTRE))

    verts.append(CENTRE.copy())

    xy = np.array(verts)
    z = dome(xy)

    # the rim sits on the sole
    z[:n] = SOLE + spring(xy[:n, 0])

    v3 = np.column_stack([xy, z])

    faces = []

    for k in range(rings - 1):
        for i in range(n):
            j = (i + 1) % n
            faces.append([k * n + i, k * n + j, (k + 1) * n + j, (k + 1) * n + i])

    pole = len(verts) - 1

    for i in range(n):
        j = (i + 1) % n
        faces.append([(rings - 1) * n + i, (rings - 1) * n + j, pole])

    return new_object(name, mesh_from(name, v3, faces))


def build_upper():
    """The dome of the upper, as a solid shell of canvas."""
    obj = dome_surface("upper")

    # the canvas has a thickness (inwards)
    s = obj.modifiers.new("shell", "SOLIDIFY")
    s.thickness = SHELL
    s.offset = -1.0
    s.use_even_offset = True
    s.material_offset_rim = 0
    s.material_offset = 1     # the inside is the lining

    obj.data.materials.append(MATERIALS[CANVAS])
    obj.data.materials.append(MATERIALS[LINING])

    apply_modifiers(obj)

    # the opening and the slot
    collar = prism("collar_cut", COLLAR, SOLE + 7.0, 400.0)
    slot = prism("slot_cut", SLOT, SOLE + 7.0, 400.0)

    boolean(obj, collar)
    boolean(obj, slot)

    remove(collar)
    remove(slot)

    # the holes of the eyelets, along the surface's normal
    for row in EYELETS:
        for (x, y) in row:
            centre, normal = surface_point(x, y)

            bpy.ops.mesh.primitive_cylinder_add(
                vertices=32, radius=HOLE, depth=30.0, location=centre,
                rotation=normal.to_track_quat("Z", "Y").to_euler())

            hole = bpy.context.active_object
            hole.hide_render = True

            boolean(obj, hole)
            remove(hole)

    # material numbers to the app's
    fix_materials(obj, {0: CANVAS, 1: LINING})

    return obj


def fix_materials(obj, mapping):
    # faces cut by a boolean come with the cutter's empty slot: the edge of
    # the canvas, the first material
    first = obj.data.materials[0].name
    old = [m.name if m is not None else first for m in obj.data.materials]
    indices = [p.material_index for p in obj.data.polygons]

    obj.data.materials.clear()

    for m in MATERIALS:
        obj.data.materials.append(m)

    for p, i in zip(obj.data.polygons, indices):
        name = old[i] if i < len(old) else old[0]
        p.material_index = [m.name for m in MATERIALS].index(name)


def surface_point(x, y):
    """The point of the upper's outer surface over (x, y), and its normal."""
    e = 0.5
    xy = np.array([[x, y], [x + e, y], [x - e, y], [x, y + e], [x, y - e]])
    z = dome(xy)

    gx = (z[1] - z[2]) / (2 * e)
    gy = (z[3] - z[4]) / (2 * e)

    normal = Vector((-gx, -gy, 1.0)).normalized()

    return Vector((x, y, float(z[0]))), normal


def tongue_top(xy):
    """Top of the tongue: under the flaps by the lace's room, rising out of
    the opening at the back."""
    z = dome(xy) - GAP

    # behind the slot it rises above the collar, curling a little back
    x = xy[:, 0]
    rise = 24.0 * np.clip((138.0 - x) / 22.0, 0.0, None) ** 1.2

    return z + rise


def tongue_outline():
    pts = []

    # 21 wide under the flaps, a rounded top at the back
    for x in np.linspace(236.0, 126.0, 56):
        pts.append((x, -float(table(x, [120.0, 140.0, 236.0], [18.0, 20.0, 13.0]))))

    for a in np.linspace(-math.pi / 2, -3 * math.pi / 2, 20)[1:-1]:
        pts.append((126.0 + 18.0 * math.cos(a) * 0.7, 18.0 * math.sin(a)))

    for x in np.linspace(126.0, 236.0, 56):
        pts.append((x, float(table(x, [120.0, 140.0, 236.0], [18.0, 20.0, 13.0]))))

    # counter-clockwise seen from above (the faces look up)
    return np.array(pts[::-1])


def build_tongue():
    outline = tongue_outline()
    n = len(outline)

    # rings from the edge in, like the upper
    centre = np.array([175.0, 0.0])
    rings = 14

    verts = []

    for k in range(rings):
        r = 1.0 - (k / rings) ** 1.3

        for p in outline:
            verts.append(centre + r * (p - centre))

    verts.append(centre)

    xy = np.array(verts)

    # the edge of the tongue is rounded down a little
    d = distance_to_curve(xy, outline)
    z = tongue_top(xy) - 2.0 * (1.0 - np.clip(d / 5.0, 0.0, 1.0)) ** 2

    faces = []

    for k in range(rings - 1):
        for i in range(n):
            j = (i + 1) % n
            faces.append([k * n + i, k * n + j, (k + 1) * n + j, (k + 1) * n + i])

    pole = len(verts) - 1

    for i in range(n):
        faces.append([(rings - 1) * n + i, (rings - 1) * n + (i + 1) % n, pole])

    obj = new_object("tongue", mesh_from("tongue", np.column_stack([xy, z]), faces))

    s = obj.modifiers.new("padding", "SOLIDIFY")
    s.thickness = 4.0
    s.offset = -1.0
    s.use_even_offset = True

    apply_modifiers(obj)

    # canvas on top, the lining underneath (by where each face lies)
    assign(obj, TONGUE)

    centres = np.array([[p.center.x, p.center.y] for p in obj.data.polygons])
    heights = np.array([p.center.z for p in obj.data.polygons])
    top = tongue_top(centres)

    for p, h, t in zip(obj.data.polygons, heights, top):
        if h < t - 3.0:
            p.material_index = LINING

    return obj


def build_sole():
    """The rubber sole: the footprint raised to SOLE, its bottom edge
    rounded, the top a narrow lip around the upper."""
    n = len(OUTSOLE)
    inward = inward_normals(OUTSOLE)

    # (inset, height) of the rings from the bottom up; heights are
    # fractions of the sole's height
    rings = [
        (5.0, 0.0), (2.6, 0.012), (1.0, 0.05), (0.25, 0.12), (0.0, 0.2),
        (-0.3, 0.6), (-0.3, 0.95), (0.0, 1.0),
    ]

    verts = []

    for inset, h in rings:
        for i in range(n):
            x, y = OUTSOLE[i] + inward[i] * inset
            bottom = spring(x)
            verts.append((x, y, bottom + h * SOLE))

    # the lip on top, from the edge to the upper
    for i in range(n):
        x, y = UPPER[i]
        verts.append((x, y, SOLE + spring(x)))

    faces = []

    for k in range(len(rings)):
        for i in range(n):
            j = (i + 1) % n
            a, b = k * n + i, k * n + j
            faces.append([a, b, b + n, a + n])

    # bottom: a fan around the middle
    verts.append((CENTRE[0], CENTRE[1], 0.0))
    bottom = len(verts) - 1

    for i in range(n):
        faces.append([(i + 1) % n, i, bottom])

    obj = new_object("sole", mesh_from("sole", verts, faces))

    assign(obj, RUBBER)

    return obj


def build_toecap():
    """A rubber cap over the toe, on top of the canvas: the dome thickened
    outwards, cut off along an arc across the toe box."""
    obj = dome_surface("toecap")

    s = obj.modifiers.new("rubber", "SOLIDIFY")
    s.thickness = 1.5
    s.offset = 1.0
    s.use_even_offset = True

    apply_modifiers(obj)

    # everything in front of the arc x = 250 + 0.004 y^2
    ys = np.linspace(-70.0, 70.0, 57)
    arc = [(250.0 + 0.004 * y * y, y) for y in ys]
    region = np.array(arc + [(340.0, 70.0), (340.0, -70.0)])[::-1]

    cutter = prism("cap_cut", region, -10.0, 200.0)

    boolean(obj, cutter, "INTERSECT")
    remove(cutter)

    assign(obj, TOECAP)

    return obj


def tube_along(name, points, radius, closed, mat, sides=16):
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = radius
    curve.bevel_resolution = sides // 4
    curve.use_fill_caps = True

    spline = curve.splines.new("POLY")
    spline.points.add(len(points) - 1)

    for p, q in zip(spline.points, points):
        p.co = (q[0], q[1], q[2], 1.0)

    spline.use_cyclic_u = closed

    obj = new_object(name, curve)

    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.convert(target="MESH")
    obj.select_set(False)

    for p in obj.data.polygons:
        p.use_smooth = True

    assign(obj, mat)

    return obj


def build_collar():
    """A padded roll around the heel's part of the opening, and a folded
    edge along the slot."""
    pts = []

    # the opening's outline where the upper is cut (not the slot)
    for x, y in COLLAR:
        if x > 128.0 and abs(y) < 20.0:
            continue

        pts.append((x, y))

    pts = np.array(pts)

    # from one side of the slot round the heel to the other
    order = np.argsort(np.arctan2(pts[:, 1], -(pts[:, 0] - 82.0)))
    pts = pts[order]

    z = dome(pts) - 2.0

    roll = tube_along("collar", np.column_stack([pts, z]), 4.5, False, CANVAS)

    edges = []

    for side in (1.0, -1.0):
        xs = np.linspace(128.0, 214.0, 50)
        w = slot_halfwidth(xs) + 0.6
        e = np.stack([xs, side * w], axis=1)
        edges.append(tube_along("edge", np.column_stack([e, dome(e) - 1.3]), 1.4, False, CANVAS, 8))

    return [roll] + edges


def build_eyelets():
    objs = []

    for row in EYELETS:
        for (x, y) in row:
            centre, normal = surface_point(x, y)

            bpy.ops.mesh.primitive_torus_add(
                major_radius=HOLE + 1.1, minor_radius=1.05,
                major_segments=40, minor_segments=12,
                location=centre + normal * 0.35,
                rotation=normal.to_track_quat("Z", "Y").to_euler())

            ring = bpy.context.active_object

            for p in ring.data.polygons:
                p.use_smooth = True

            assign(ring, METAL)
            objs.append(ring)

    return objs


def build_insole():
    inner = UPPER + inward_normals(UPPER) * (SHELL + 0.5)
    verts = [(x, y, SOLE + 4.0 + spring(x)) for x, y in inner]
    verts.append((CENTRE[0], CENTRE[1], SOLE + 4.0))

    n = len(inner)
    faces = [[i, (i + 1) % n, n] for i in range(n)]

    obj = new_object("insole", mesh_from("insole", verts, faces))
    assign(obj, LINING)

    return obj


# --------------------------------------------------------------------------

def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def build():
    clear_scene()

    parts = [build_upper(), build_tongue(), build_sole(), build_toecap(), build_insole()]
    parts += build_collar()
    parts += build_eyelets()

    for p in parts:
        p.select_set(True)

    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()

    shoe = bpy.context.active_object
    shoe.name = "sneaker"

    bpy.ops.object.select_all(action="DESELECT")

    return shoe


def setup_world():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.unit_settings.system = "NONE"

    world = bpy.data.worlds.new("room")
    scene.world = world
    world.use_nodes = True

    # the room of the scene (for the previews)
    hdr = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "data", "photo", "country_club_4k.hdr")
    nodes = world.node_tree.nodes
    links = world.node_tree.links

    if os.path.exists(hdr):
        env = nodes.new("ShaderNodeTexEnvironment")
        env.image = bpy.data.images.load(hdr)
        links.new(env.outputs["Color"], nodes["Background"].inputs["Color"])

    world.light_settings.distance = 25.0


def bake_occlusion(shoe):
    """Ambient occlusion of the shoe (per corner), and the floor's view of
    the room past the shoe (a picture under it)."""
    scene = bpy.context.scene
    scene.cycles.samples = 128
    scene.render.bake.target = "VERTEX_COLORS"

    # a floor under the shoe (it darkens the sole's lower edge)
    bpy.ops.mesh.primitive_plane_add(size=1.0, location=(145.0, 0.0, 0.0))
    floor = bpy.context.active_object
    floor.scale = (FLOOR_X[1] - FLOOR_X[0], FLOOR_Y[1] - FLOOR_Y[0], 1.0)
    floor.location = (0.5 * (FLOOR_X[0] + FLOOR_X[1]), 0.5 * (FLOOR_Y[0] + FLOOR_Y[1]), 0.0)
    bpy.ops.object.transform_apply(scale=True)

    floor_mat = material("floor", (0.5, 0.5, 0.5), 0.8)
    floor.data.materials.append(floor_mat)

    shoe.data.color_attributes.new("ao", "BYTE_COLOR", "CORNER")
    shoe.data.color_attributes.active_color_name = "ao"

    bpy.ops.object.select_all(action="DESELECT")
    shoe.select_set(True)
    bpy.context.view_layer.objects.active = shoe

    scene.world.light_settings.distance = 25.0
    bpy.ops.object.bake(type="AO")

    # the floor: a picture, its corners FLOOR_X x FLOOR_Y
    image = bpy.data.images.new("floor_ao", FLOOR_PIXELS[0], FLOOR_PIXELS[1], float_buffer=True)

    floor_mat.use_nodes = True
    node = floor_mat.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = image
    floor_mat.node_tree.nodes.active = node

    scene.render.bake.target = "IMAGE_TEXTURES"
    scene.render.bake.margin = 4
    scene.cycles.samples = 1024
    scene.world.light_settings.distance = 120.0

    bpy.ops.object.select_all(action="DESELECT")
    floor.select_set(True)
    bpy.context.view_layer.objects.active = floor

    bpy.ops.object.bake(type="AO")

    image.filepath_raw = os.path.join(OUT, "sneaker_floor.png")
    image.file_format = "PNG"
    scene.render.image_settings.color_depth = "16"
    image.save()

    return floor


# the floor's picture: x from heel to toe and y across (Blender), in mm
FLOOR_X = (-80.0, 370.0)
FLOOR_Y = (-150.0, 150.0)
FLOOR_PIXELS = (768, 512)


def export(shoe):
    """sneaker.bin: 'AKSHOE01', vertex count, index count, then per vertex
    position (3), normal (3), occlusion, material (floats), then the
    triangles (uint32). The app's frame: x = x, y = z, z = -y."""
    mesh = shoe.data

    mesh.set_sharp_from_angle(angle=math.radians(50.0))

    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(mesh)
    bm.free()

    mesh.update()

    normals = mesh.corner_normals
    ao = mesh.color_attributes["ao"].data

    vertices = []
    lookup = {}
    indices = []

    for poly in mesh.polygons:
        for li in poly.loop_indices:
            loop = mesh.loops[li]
            v = mesh.vertices[loop.vertex_index].co
            n = normals[li].vector
            o = ao[li].color[0]

            key = (loop.vertex_index, round(n.x, 3), round(n.y, 3), round(n.z, 3),
                   round(o, 2), poly.material_index)

            if key not in lookup:
                lookup[key] = len(vertices)
                vertices.append((v.x, v.z, -v.y, n.x, n.z, -n.y, o, float(poly.material_index)))

            indices.append(lookup[key])

    with open(os.path.join(OUT, "sneaker.bin"), "wb") as f:
        f.write(b"AKSHOE01")
        f.write(struct.pack("<II", len(vertices), len(indices)))

        for v in vertices:
            f.write(struct.pack("<8f", *v))

        f.write(struct.pack("<%dI" % len(indices), *indices))

    print("sneaker.bin: %d vertices, %d triangles" % (len(vertices), len(indices) // 3))


def write_guide():
    """sneaker.json: the eyelets (centre on the flap, the hole's axis) and
    the tongue's top under the slot, for the lacing; all in the app's frame
    (x = x, y = z, z = -y), millimetres."""

    def app(v):
        return [round(v[0], 3), round(v[2], 3), round(-v[1], 3)]

    eyelets = []

    for k, row in enumerate(EYELETS):
        for side, (x, y) in zip(("left", "right"), row):
            centre, normal = surface_point(x, y)
            # "edge": how far the slot's edge is from the middle there
            eyelets.append({"row": k, "side": side, "centre": app(centre), "axis": app(normal),
                            "edge": round(float(slot_halfwidth(x)), 3)})

    # the tongue's top on a grid (x along, z across in the app's frame)
    xs = np.arange(120.0, 226.0, 2.0)
    zs = np.arange(-30.0, 31.0, 2.0)

    grid = []

    for x in xs:
        row = np.array([[x, -z] for z in zs])
        grid.append([round(float(v), 3) for v in tongue_top(row)])

    guide = {
        "lace": LACE,
        "hole": HOLE,
        "shell": SHELL,
        "eyelets": eyelets,
        "tongue": {"x0": float(xs[0]), "z0": float(zs[0]), "step": 2.0,
                   "nx": len(xs), "nz": len(zs), "top": grid},
        # the floor picture's corners (app frame: x, z)
        "floor": {"x0": FLOOR_X[0], "x1": FLOOR_X[1], "z0": -FLOOR_Y[1], "z1": -FLOOR_Y[0]},
    }

    with open(os.path.join(OUT, "sneaker.json"), "w") as f:
        json.dump(guide, f, indent=1)


def preview(shoe, floor):
    if not PREVIEW:
        return

    os.makedirs(PREVIEW, exist_ok=True)

    scene = bpy.context.scene
    scene.cycles.samples = 48
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 750
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.view_transform = "AgX"
    scene.world.light_settings.distance = 25.0

    cam_data = bpy.data.cameras.new("camera")
    cam_data.lens = 50.0
    cam_data.clip_start = 1.0
    cam_data.clip_end = 100000.0
    cam = new_object("camera", cam_data)
    scene.camera = cam

    target = Vector((150.0, 0.0, 45.0))

    for name, eye in (("side", (330.0, -420.0, 230.0)),
                      ("top", (190.0, -160.0, 520.0)),
                      ("back", (-250.0, 260.0, 300.0))):
        cam.location = eye
        cam.rotation_euler = (target - Vector(eye)).to_track_quat("-Z", "Y").to_euler()

        scene.render.filepath = os.path.join(PREVIEW, "sneaker_%s.png" % name)
        bpy.ops.render.render(write_still=True)


shoe = build()
setup_world()

write_guide()

floor = bake_occlusion(shoe)

preview(shoe, floor)

export(shoe)
