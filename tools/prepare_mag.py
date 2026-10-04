# Prepares the MP7 40-round magazine's runtime assets for the GML plugin (no cooking, no pak). They are
# the input to tools/pack, which encrypts them into the DLL; nothing here ships as loose files.
#
#   Assets/mp7mag40.obj                 the magazine, in the game's MP7 magazine mesh space
#                                       (MP7A2_Mag_20), Blender default OBJ axes, one group "MI_Mag"
#   Assets/Mag_Diffuse/Normal/ORM.png   textures for the game's weapon shader M_DefaultShader
#
# Input: a .blend holding the MP7 model's "Magazine" object, with its textures packed (the model's
# Blender scene). Set MP7_BLEND to it. The file itself is not changed.
#
# Fit: the model is about 10% larger than the game's MP7 (its grip is 3.15 cm wide where the game's
# is 2.84), so it is scaled by 0.90, which also makes its baseplate as wide as the game's. It is placed
# where it leaves the grip: the model's grip ends at z = 9.27 cm, the game's 20-round magazine leaves
# the grip at the top of its baseplate (z = -8.0 in its mesh space), and front to back the two
# magazine bodies are centred on each other there. The rest of the 40 hangs below, as on the real gun.
#
# Textures: the model shares one 4K atlas across the whole gun, and the magazine's pieces are spread
# over all of it (8% of its area). So the magazine gets a UV layout of its own and its colour,
# roughness, metallic and normal are baked onto it at 1024 (Cycles; the normal in tangent space, so
# it stays right on the new layout). The game imports loose PNGs as sRGB textures, so Normal and ORM
# (linear data) are pre-encoded with the sRGB curve here. Blender's normals are OpenGL-style; UE's are
# DirectX-style, so green is flipped.
#
# Run:  set MP7_BLEND=...\mp7.blend
#       blender.exe --background --python tools/prepare_mag.py
import bpy, math, os
import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.environ.get("MP7_BLEND", "")
OUT = os.path.join(REPO, "Assets")
if not os.path.isfile(SRC):
    raise SystemExit("set MP7_BLEND to the .blend holding the MP7's Magazine object")
os.makedirs(OUT, exist_ok=True)

SCALE = 0.90
MODEL_GRIP_BOTTOM = 9.27   # cm, model space (Blender z)
GAME_GRIP_BOTTOM = -8.0    # cm, game magazine mesh space (UE z): top of the 20-round's baseplate
GAME_BODY_CENTRE = 0.36    # cm, UE x: centre of the 20-round's body front to back just above its baseplate
TEX_SIZE = 1024
BAKE_MARGIN = 8            # texels painted out around each island (filtering, no mips)

bpy.ops.wm.open_mainfile(filepath=SRC)
mag = bpy.data.objects.get("Magazine")
if not mag or mag.type != 'MESH':
    raise SystemExit("no 'Magazine' mesh object in " + SRC)
mesh = mag.data
mat = mesh.materials[0]
nodes, links = mat.node_tree.nodes, mat.node_tree.links

# ---- textures: a UV layout of its own, baked from the atlas
original_name = mesh.uv_layers.active.name  # (layer references go stale once another is added)
mesh.uv_layers[original_name].active_render = True  # the material keeps reading the atlas through this one
packed = mesh.uv_layers.new(name="Packed")
mesh.uv_layers.active = packed           # bakes write through this one
for o in bpy.context.view_layer.objects:
    o.select_set(o == mag)
bpy.context.view_layer.objects.active = mag
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.004, area_weight=0.0, correct_aspect=True)
bpy.ops.object.mode_set(mode='OBJECT')

scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 4
scene.render.bake.margin = BAKE_MARGIN
scene.render.bake.use_clear = True
scene.render.bake.use_selected_to_active = False

out_node = next(n for n in nodes if n.type == 'OUTPUT_MATERIAL')
bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
tex = {n.image.name.split(".png")[0].replace("Mp7M_", ""): n for n in nodes if n.type == 'TEX_IMAGE' and n.image}
target_node = nodes.new("ShaderNodeTexImage")
emit = nodes.new("ShaderNodeEmission")

def bake(kind, source=None):
    img = bpy.data.images.new("bake_" + (source or kind), TEX_SIZE, TEX_SIZE, alpha=False, float_buffer=True)
    img.colorspace_settings.name = 'Non-Color'  # raw values out
    target_node.image = img
    nodes.active = target_node
    if source:  # the texture's own value, passed straight out
        for l in list(out_node.inputs["Surface"].links):
            links.remove(l)
        links.new(tex[source].outputs["Color"], emit.inputs["Color"])
        links.new(emit.outputs["Emission"], out_node.inputs["Surface"])
        bpy.ops.object.bake(type='EMIT')
    else:       # the shading normal (atlas normal map included), in the new layout's tangent space
        for l in list(out_node.inputs["Surface"].links):
            links.remove(l)
        links.new(bsdf.outputs["BSDF"], out_node.inputs["Surface"])
        bpy.ops.object.bake(type='NORMAL', normal_space='TANGENT')
    px = np.array(img.pixels[:], dtype=np.float32).reshape(TEX_SIZE, TEX_SIZE, 4)
    bpy.data.images.remove(img)
    print("BAKED", source or kind)
    return px

def to_srgb(v):
    return np.where(v <= 0.0031308, v * 12.92, 1.055 * np.power(np.clip(v, 0, 1), 1 / 2.4) - 0.055)

def save(arr, name):
    img = bpy.data.images.new(name, TEX_SIZE, TEX_SIZE, alpha=True)
    img.colorspace_settings.name = 'Non-Color'
    img.pixels[:] = np.clip(arr, 0, 1).ravel()
    img.filepath_raw = os.path.join(OUT, name + ".png")
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)
    print("WROTE", name)

d = bake("color", "BaseColor")             # linear (the atlas is sRGB): back to sRGB for the game
d[..., :3] = to_srgb(d[..., :3]); d[..., 3] = 1
save(d, "Mag_Diffuse")
n = bake("normal")
n[..., 1] = 1 - n[..., 1]                  # OpenGL -> DirectX
n[..., :3] = to_srgb(n[..., :3]); n[..., 3] = 1
save(n, "Mag_Normal")
orm = np.ones((TEX_SIZE, TEX_SIZE, 4), dtype=np.float32)
orm[..., 1] = bake("roughness", "Roughness")[..., 0]
orm[..., 2] = bake("metallic", "Metallic")[..., 0]
orm[..., :3] = to_srgb(orm[..., :3])
save(orm, "Mag_ORM")
mesh.uv_layers.remove(mesh.uv_layers[original_name])
mesh.uv_layers["Packed"].active_render = True

# ---- geometry: into the game's magazine mesh space
M = np.array(mag.matrix_world)
co = np.array([tuple(v.co) + (1.0,) for v in mesh.vertices]) @ M.T
m = co[:, :3] * 100  # model, cm: x right, y forward, z up

# Front-to-back centre of the model's body where it leaves its grip (the slice just above).
band = m[(m[:, 2] >= MODEL_GRIP_BOTTOM) & (m[:, 2] < MODEL_GRIP_BOTTOM + 1.5)]
model_centre = (band[:, 1].min() + band[:, 1].max()) / 2
model_side = (m[:, 0].min() + m[:, 0].max()) / 2
t = np.array([GAME_BODY_CENTRE - SCALE * model_centre, -SCALE * model_side, GAME_GRIP_BOTTOM - SCALE * MODEL_GRIP_BOTTOM])
ue = np.stack([m[:, 1], m[:, 0], m[:, 2]], 1) * SCALE + t  # UE: x forward, y right, z up
print(f"model centre at the grip exit {model_centre:.2f}, offset {np.round(t, 3)}")
print(f"magazine in UE mesh space: X[{ue[:,0].min():.2f}..{ue[:,0].max():.2f}] Y[{ue[:,1].min():.2f}..{ue[:,1].max():.2f}] Z[{ue[:,2].min():.2f}..{ue[:,2].max():.2f}]")

# Back to Blender coordinates that GML's BlenderObj import (UE = (x, -y, z) * 100) turns into `ue`.
# In Blender terms this is a turn of -90 degrees about Z (muzzle +Y to +X) plus the fit: no mirroring.
blender = np.stack([ue[:, 0], -ue[:, 1], ue[:, 2]], 1) / 100
mag.matrix_world = np.identity(4).tolist()
mag.parent = None
for v, p in zip(mesh.vertices, blender):
    v.co = p
mesh.update()

# One material group the plugin knows by name.
mat.name = "MI_Mag"
bpy.ops.wm.obj_export(filepath=os.path.join(OUT, "mp7mag40.obj"), export_selected_objects=True, export_materials=True,
                      export_material_groups=False, export_uv=True, export_normals=True, forward_axis='NEGATIVE_Z',
                      up_axis='Y', global_scale=1.0, path_mode='STRIP')
print("WROTE mp7mag40.obj")
