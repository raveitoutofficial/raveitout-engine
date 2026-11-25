# noteskin3d: turn a 2D pump noteskin into a 3D one

These scripts built `NoteSkins/pump/rio-3d` from the 2D `rio` noteskin.

- `noteskin3d_prep.py`: traces a sprite sheet's outline and writes padded texture frames, an animated
  texture `.ini`, side-wall colours and the shared `_shine sphere.png`.
- `noteskin3d_blender.py`: runs in Blender (headless). Extrudes and bevels the outline and exports a
  Milkshape ASCII model the engine loads. `--lit` is for notes drawn inside a
  `Def.ActorFrame{ Lighting=true }`, like rio-3d. `--render` writes preview images.
- `noteskin3d_holds.py`: hold/roll bodies and caps are drawn by the engine as texture strips, not
  models. This re-renders each frame as a lit, beveled bar, so they look 3D in normal play.

Needs Blender 5.x and Python with numpy, pillow, scikit-image and mapbox-earcut (a venv is fine).

Example (one tap note):

    python noteskin3d_prep.py "NoteSkins/pump/rio/Center Tap Note 3x2 (doubleres).png" 3 2 NoteSkins/pump/rio-3d Center
    blender -b --factory-startup --python noteskin3d_blender.py -- NoteSkins/pump/rio-3d Center --lit
    python noteskin3d_holds.py "Center Hold Body Active 6x1 (doubleres).png" 6 1 body "Center Hold Body Active 6x1 (doubleres).png"
