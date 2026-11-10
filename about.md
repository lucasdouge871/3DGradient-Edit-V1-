# 3DGradient Edit

Build 3D shapes in the Geometry Dash editor, with gradient triggers.

Press the **3DGradient Edit** button in the editor (a cube icon next to Undo / Redo) or press **F6** on PC, pick a shape,
set its **Color** and **Shadow**, and press **Convert into gradient triggers**.

## What it does

- **Live 3D preview.** Drag the preview to rotate the shape. What you see is exactly what gets built.
- **Shapes.** Cube, Box, Pyramid, Tetrahedron, Octahedron, Triangular Prism, Hexagonal Prism, Cylinder.
- **Edit points.** Select a point in the list, change its X / Y / Z, duplicate it, add one, or remove one. Faces are rebuilt automatically.
- **Color + Shadow.** Two color pickers (saturation square, hue bar, opacity and RGB sliders, hex). The mod picks unused **color channel IDs** for them on its own (and for any in-between shades).
- **One click convert.** Every point becomes an object (ID 3802 by default) with its **own group ID**, and every face becomes a **gradient trigger** that already points at those groups.

## Settings

Point object ID, gradient trigger ID, shape size, number of shade levels, smooth shading, perspective,
and where the editor button sits.

Made for Geometry Dash **2.2081**.
