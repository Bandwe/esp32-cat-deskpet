"""Render the actual printable STL files. No concept-image geometry is substituted.

Assembly axes: X horizontal, Y up, Z toward the front; front surface at Z=0.
The optional display insert is illustrative only; it is not exported for printing.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import vtk
from PIL import Image, ImageDraw, ImageFont


SHELL = (0.115, 0.129, 0.143)
LID = (0.155, 0.169, 0.184)
STAND = (0.185, 0.199, 0.213)
FLOOR = (0.74, 0.72, 0.68)


def read_stl(path: Path) -> vtk.vtkPolyData:
    reader = vtk.vtkSTLReader()
    reader.SetFileName(str(path))
    reader.Update()
    if reader.GetOutput().GetNumberOfPoints() == 0:
        raise RuntimeError(f"No triangles in {path}")
    output = vtk.vtkPolyData()
    output.DeepCopy(reader.GetOutput())
    return output


def actor_for(poly, color=SHELL, offset=(0, 0, 0), flat=False):
    # Feature-angle normals soften tessellation without rounding actual sharp edges.
    normals = vtk.vtkPolyDataNormals()
    normals.SetInputData(poly)
    normals.SetFeatureAngle(55)
    normals.SplittingOn()
    normals.ConsistencyOn()
    normals.AutoOrientNormalsOn()
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputConnection(normals.GetOutputPort())
    actor = vtk.vtkActor()
    actor.SetMapper(mapper)
    actor.SetPosition(*offset)
    prop = actor.GetProperty()
    prop.SetColor(*color)
    prop.SetInterpolationToPhong()
    prop.SetAmbient(1.0 if flat else 0.26)
    prop.SetDiffuse(0.0 if flat else 0.74)
    prop.SetSpecular(0.0 if flat else 0.22)
    prop.SetSpecularPower(28)
    return actor


def polygon(points, color, flat=True):
    vertices = vtk.vtkPoints()
    cell = vtk.vtkPolygon()
    cell.GetPointIds().SetNumberOfIds(len(points))
    for i, p in enumerate(points):
        vertices.InsertNextPoint(*p)
        cell.GetPointIds().SetId(i, i)
    cells = vtk.vtkCellArray()
    cells.InsertNextCell(cell)
    mesh = vtk.vtkPolyData()
    mesh.SetPoints(vertices)
    mesh.SetPolys(cells)
    triangles = vtk.vtkTriangleFilter()
    triangles.SetInputData(mesh)
    triangles.Update()
    return actor_for(triangles.GetOutput(), color, flat=flat)


def rounded_rect(cx, cy, z, width, height, radius, color):
    points = []
    for x, y, start in [
        (cx + width / 2 - radius, cy + height / 2 - radius, 0),
        (cx - width / 2 + radius, cy + height / 2 - radius, 90),
        (cx - width / 2 + radius, cy - height / 2 + radius, 180),
        (cx + width / 2 - radius, cy - height / 2 + radius, 270),
    ]:
        for i in range(17):
            angle = math.radians(start + i * 90 / 16)
            points.append((x + radius * math.cos(angle), y + radius * math.sin(angle), z))
    return polygon(points, color)


def ellipse(cx, cy, z, rx, ry, color):
    return polygon(
        [(cx + rx * math.cos(a * math.tau / 96),
          cy + ry * math.sin(a * math.tau / 96), z) for a in range(96)],
        color,
    )


def screen_actors(args):
    x, y, z, width, height = args.screen
    actors = [rounded_rect(x, y, z, width, height, min(5, width / 7), (0.018, 0.023, 0.027))]
    for direction in (-1, 1):
        eye_x = x + direction * width * 0.235
        eye_y = y + height * 0.02
        actors.append(ellipse(eye_x, eye_y, z + 0.02, width * 0.118, width * 0.139, (0.97, 0.78, 0.34)))
        actors.append(ellipse(eye_x, eye_y + 0.15, z + 0.04, width * 0.077, width * 0.112, (0.007, 0.010, 0.011)))
        actors.append(ellipse(eye_x - width * 0.037, eye_y + width * 0.062, z + 0.06, width * 0.020, width * 0.022, (0.98, 0.98, 0.96)))
    return actors


def combined_bounds(actors):
    bounds = [a.GetBounds() for a in actors]
    return tuple((min(b[i] for b in bounds) if i % 2 == 0 else max(b[i] for b in bounds)) for i in range(6))


def render(actors, output: Path, direction, floor=True, scale=1.0, size=(1500, 1500)):
    bounds = combined_bounds(actors)
    center = tuple((bounds[i] + bounds[i + 1]) / 2 for i in (0, 2, 4))
    span = max(bounds[i + 1] - bounds[i] for i in (0, 2, 4))
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(0.88, 0.87, 0.84)
    renderer.SetBackground2(0.965, 0.953, 0.927)
    renderer.GradientBackgroundOn()
    for actor in actors:
        renderer.AddActor(actor)
    if floor:
        plane = vtk.vtkPlaneSource()
        level = bounds[2] - 0.05
        plane.SetOrigin(-220, level, -200)
        plane.SetPoint1(220, level, -200)
        plane.SetPoint2(-220, level, 240)
        plane.Update()
        floor_actor = actor_for(plane.GetOutput(), FLOOR)
        floor_actor.GetProperty().SetSpecular(0)
        renderer.AddActor(floor_actor)

    camera = renderer.GetActiveCamera()
    camera.SetFocalPoint(*center)
    camera.SetPosition(*(center[i] + direction[i] * span * 3 for i in range(3)))
    camera.SetViewUp(0, 1, 0)
    camera.ParallelProjectionOn()
    camera.SetParallelScale(span * 0.66 * scale)
    renderer.AutomaticLightCreationOff()
    for position, intensity in [((-1.3, 1.8, 2.0), 0.88), ((1.8, 0.5, 0.5), 0.42), ((0.4, 1.5, -2.0), 0.72)]:
        light = vtk.vtkLight()
        light.SetLightTypeToSceneLight()
        light.SetPosition(*(center[i] + position[i] * span * 3 for i in range(3)))
        light.SetFocalPoint(*center)
        light.SetIntensity(intensity)
        renderer.AddLight(light)

    window = vtk.vtkRenderWindow()
    window.SetOffScreenRendering(1)
    window.SetSize(*size)
    window.SetMultiSamples(8)
    window.AddRenderer(renderer)
    renderer.ResetCameraClippingRange()
    window.Render()
    capture = vtk.vtkWindowToImageFilter()
    capture.SetInput(window)
    capture.SetInputBufferTypeToRGB()
    capture.ReadFrontBufferOff()
    capture.Update()
    writer = vtk.vtkPNGWriter()
    writer.SetFileName(str(output))
    writer.SetInputConnection(capture.GetOutputPort())
    writer.Write()
    window.Finalize()


def annotate(path, title, subtitle):
    im = Image.open(path).convert("RGB")
    d = ImageDraw.Draw(im)
    font_path = Path("C:/Windows/Fonts/arial.ttf")
    font = ImageFont.truetype(str(font_path), 40) if font_path.exists() else ImageFont.load_default()
    small = ImageFont.truetype(str(font_path), 23) if font_path.exists() else ImageFont.load_default()
    d.text((60, 47), title, fill=(38, 42, 45), font=font)
    d.text((62, 101), subtitle, fill=(90, 94, 95), font=small)
    im.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-dir", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--print-dir", type=Path, help="Optional print-oriented STL directory (Z up)")
    parser.add_argument("--screen", type=float, nargs=5, metavar=("X", "Y", "Z", "W", "H"),
                        help="Illustrative display center and size in mm; only added to assembly view")
    parser.add_argument("--body", default="body.stl")
    parser.add_argument("--lid", default="lid.stl")
    parser.add_argument("--stand", default="stand.stl")
    parser.add_argument("--stand-offset", type=float, nargs=3, default=(0, 0, 0))
    parser.add_argument("--lid-offset", type=float, nargs=3, default=(0, 0, 0))
    args = parser.parse_args()
    out = args.output_dir or args.model_dir / "preview"
    out.mkdir(parents=True, exist_ok=True)
    models = {name: read_stl(args.model_dir / filename) for name, filename in
              [("body", args.body), ("lid", args.lid), ("stand", args.stand)]}
    for name, mesh in models.items():
        print(name, "bounds", tuple(round(n, 3) for n in mesh.GetBounds()), "triangles", mesh.GetNumberOfCells())

    assembly = [actor_for(models["body"], SHELL),
                actor_for(models["lid"], LID, args.lid_offset),
                actor_for(models["stand"], STAND, args.stand_offset)]
    if args.screen:
        assembly.extend(screen_actors(args))
    render(assembly, out / "01_assembled_front.png", (-0.72, 0.35, 1.65))
    annotate(out / "01_assembled_front.png", "CAT CASE / V01", "Actual STL geometry  |  Display and cat eyes are illustrative")

    explode_offset = tuple(args.lid_offset[i] + (0, 4, -22)[i] for i in range(3))
    exploded = [actor_for(models["body"], SHELL), actor_for(models["lid"], LID, explode_offset),
                actor_for(models["stand"], STAND, args.stand_offset)]
    render(exploded, out / "02_exploded_rear.png", (1.2, 0.72, -1.65), floor=False)
    annotate(out / "02_exploded_rear.png", "REAR / SEPARATE LID", "Actual STL geometry  |  Lid pulled away for structural view")

    parts = []
    cursor = 0
    for name, color in [("body", SHELL), ("lid", LID), ("stand", STAND)]:
        mesh = models[name]
        if args.print_dir:
            filename = getattr(args, name)
            mesh = read_stl(args.print_dir / filename)
            transform = vtk.vtkTransform()
            transform.RotateX(-90)
            rotate = vtk.vtkTransformPolyDataFilter()
            rotate.SetInputData(mesh)
            rotate.SetTransform(transform)
            rotate.Update()
            mesh = rotate.GetOutput()
        b = mesh.GetBounds()
        offset = (cursor - b[0], -b[2], -((b[4] + b[5]) / 2))
        parts.append(actor_for(mesh, color, offset))
        cursor += b[1] - b[0] + 12
    render(parts, out / "03_print_parts.png", (-0.45, 0.70, 1.65), size=(1800, 1200), scale=0.70)
    subtitle = "Body + rear lid + stand  |  Print-oriented STL files" if args.print_dir else "Body + rear lid + stand  |  Arrangement view, not prescribed print orientation"
    annotate(out / "03_print_parts.png", "THREE PRINTABLE PARTS", subtitle)
    images = [Image.open(out / n).convert("RGB") for n in ["01_assembled_front.png", "02_exploded_rear.png"]]
    contact = Image.new("RGB", (1800, 900), "white")
    for idx, im in enumerate(images):
        contact.paste(im.resize((900, 900), Image.Resampling.LANCZOS), (idx * 900, 0))
    contact.save(out / "cat_case_v01_preview.png")
    print("Rendered preview files in", out)


if __name__ == "__main__":
    main()
