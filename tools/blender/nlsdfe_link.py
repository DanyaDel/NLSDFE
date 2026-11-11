# NLSDFE <-> Blender: живая связка с редактором движка.
#
# Движок запускает:  blender --python nlsdfe_link.py -- <exchange.json>
# Аддон строит сцену из файла обмена, сохраняет .blend рядом с ним (чтобы Ctrl+S
# работал без диалогов) и при каждом сохранении отдаёт результат движку:
#   SDF-модель -> result.json (примитивы, матрицы, параметры, материалы)
#   меш        -> result.obj
# Движок следит за временем изменения результата и сам подхватывает правки.
#
# --selftest: построить сцену, сразу выгрузить результат и выйти (для проверок без окна).

import bpy
import bmesh
import json
import math
import os
import sys
from mathutils import Matrix

TYPES = [("sphere", "Sphere", ""), ("box", "Box", ""), ("cylinder", "Cylinder", ""),
         ("torus", "Torus", ""), ("capsule", "Capsule", ""), ("cone", "Cone", ""), ("volume", "Volume", "")]
OPS = [("union", "Union", ""), ("subtract", "Subtract", ""), ("intersect", "Intersect", "")]

STATE = {"exchange": None, "data": None, "dir": None, "quiet_save": False}


# ---------------------------------------------------------------------------
# Геометрия примитивов: те же размеры, что у SDF в движке (ось формы — Z)
# ---------------------------------------------------------------------------
def build_primitive(mesh, kind, size, p2):
    bm = bmesh.new()
    if kind == "sphere":
        bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=size)
    elif kind == "box":
        bmesh.ops.create_cube(bm, size=2.0 * size)
    elif kind == "cylinder":
        bmesh.ops.create_cone(bm, cap_ends=True, segments=32, radius1=size, radius2=size, depth=2.0 * size)
    elif kind == "cone":
        bmesh.ops.create_cone(bm, cap_ends=True, segments=32, radius1=size, radius2=max(size * p2, 0.0), depth=2.0 * size)
    elif kind == "capsule":
        # Сфера с нечётным числом колец: нет кольца на экваторе, половинки разносятся без разрыва
        r = size * p2
        bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=17, radius=r)
        for v in bm.verts:
            v.co.z += size if v.co.z > 0.0 else -size
    elif kind == "torus":
        major, minor = size, size * p2
        ring, tube = 48, 16
        verts = []
        for i in range(ring):
            a = 2.0 * math.pi * i / ring
            for j in range(tube):
                b = 2.0 * math.pi * j / tube
                r = major + minor * math.cos(b)
                verts.append(bm.verts.new((r * math.cos(a), r * math.sin(a), minor * math.sin(b))))
        for i in range(ring):
            for j in range(tube):
                a = verts[i * tube + j]
                b = verts[((i + 1) % ring) * tube + j]
                c = verts[((i + 1) % ring) * tube + (j + 1) % tube]
                d = verts[i * tube + (j + 1) % tube]
                bm.faces.new((a, b, c, d))
    else:  # volume: только габаритный куб [-1, 1], сама форма живёт в движке
        bmesh.ops.create_cube(bm, size=2.0)
    bm.to_mesh(mesh)
    bm.free()
    for poly in mesh.polygons:
        poly.use_smooth = kind not in ("box", "volume")


def principled(material):
    if material is None or material.node_tree is None:
        return None
    return next((n for n in material.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)


def set_color(obj, rgb, metallic, roughness):
    mat = obj.active_material
    if mat is None:
        mat = bpy.data.materials.new(obj.name)
        if mat.node_tree is None:   # В Blender 5 у новых материалов дерево узлов уже есть
            mat.use_nodes = True
        obj.data.materials.append(mat)
    mat.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)   # Цвет во вьюпорте Solid
    mat.metallic = metallic
    mat.roughness = roughness
    bsdf = principled(mat)
    if bsdf:
        for socket in bsdf.inputs:
            if socket.identifier == "Base Color":
                socket.default_value = (rgb[0], rgb[1], rgb[2], 1.0)
            elif socket.identifier == "Metallic":
                socket.default_value = metallic
            elif socket.identifier == "Roughness":
                socket.default_value = roughness


def get_color(obj):
    mat = obj.active_material
    bsdf = principled(mat)
    if bsdf:
        values = {s.identifier: s.default_value for s in bsdf.inputs if s.identifier in ("Base Color", "Metallic", "Roughness")}
        c = values.get("Base Color", (0.8, 0.8, 0.8, 1.0))
        return [c[0], c[1], c[2]], float(values.get("Metallic", 0.0)), float(values.get("Roughness", 0.5))
    if mat:
        c = mat.diffuse_color
        return [c[0], c[1], c[2]], mat.metallic, mat.roughness
    return [0.8, 0.8, 0.8], 0.0, 0.5


def refresh_display(obj):
    # Вычитание и пересечение — «инструменты», их показываем каркасом
    obj.display_type = "WIRE" if obj.nls_op != "union" or obj.nls_type == "volume" else "TEXTURED"


def on_shape_changed(self, context):
    if self.nls_managed and self.type == "MESH":
        build_primitive(self.data, self.nls_type, self.nls_size, self.nls_p2)
        refresh_display(self)


def on_op_changed(self, context):
    refresh_display(self)


# ---------------------------------------------------------------------------
# Сцена из файла обмена
# ---------------------------------------------------------------------------
def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def matrix_from_rows(rows):
    return Matrix([rows[0:4], rows[4:8], rows[8:12], rows[12:16]])


def build_sdf(data):
    collection = bpy.context.scene.collection
    by_id = {}
    for node in data["nodes"]:
        mesh = bpy.data.meshes.new(node["name"])
        obj = bpy.data.objects.new(node["name"], mesh)
        collection.objects.link(obj)
        obj.nls_managed = True
        obj.nls_id = node["id"]
        obj.nls_type = node["type"]
        obj.nls_size = node["size"]
        obj.nls_p2 = node["p2"]
        obj.nls_op = node["op"]
        obj.nls_smooth = node["smooth"]
        build_primitive(mesh, obj.nls_type, obj.nls_size, obj.nls_p2)
        set_color(obj, node["color"], node["metallic"], node["roughness"])
        refresh_display(obj)
        by_id[node["id"]] = (obj, node)
    # Родители — после того, как все объекты созданы; мировые матрицы сохраняются
    for obj, node in by_id.values():
        if node["parent"] in by_id:
            obj.parent = by_id[node["parent"]][0]
    bpy.context.view_layer.update()
    for obj, node in by_id.values():
        obj.matrix_world = matrix_from_rows(node["matrix"])
        bpy.context.view_layer.update()


def build_mesh(data):
    path = os.path.join(STATE["dir"], data["obj"])
    bpy.ops.wm.obj_import(filepath=path, forward_axis="NEGATIVE_Z", up_axis="Y")
    imported = [o for o in bpy.context.selected_objects if o.type == "MESH"]
    if imported:
        obj = imported[0]
        obj.name = data["name"]
        obj.nls_mesh = True


# ---------------------------------------------------------------------------
# Результат для движка
# ---------------------------------------------------------------------------
def export_sdf(data):
    nodes = []
    skipped = 0
    for obj in bpy.context.scene.objects:
        if not obj.nls_managed:
            skipped += 1
            continue
        rgb, metallic, roughness = get_color(obj)
        m = obj.matrix_world
        parent = obj.parent.name if obj.parent and obj.parent.nls_managed else ""
        nodes.append({
            "key": obj.name, "id": obj.nls_id, "name": obj.name, "parent": parent,
            "type": obj.nls_type, "size": obj.nls_size, "p2": obj.nls_p2, "op": obj.nls_op, "smooth": obj.nls_smooth,
            "matrix": [m[r][c] for r in range(4) for c in range(4)],
            "color": rgb, "metallic": metallic, "roughness": roughness,
        })
    path = os.path.join(STATE["dir"], data["result"])
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump({"version": 1, "kind": "sdf", "nodes": nodes}, f)
    os.replace(tmp, path)   # Движок не увидит наполовину записанный файл
    return len(nodes), skipped


def export_mesh(data):
    targets = [o for o in bpy.context.scene.objects if o.nls_mesh and o.type == "MESH"]
    if not targets:
        targets = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not targets:
        return 0, 0
    for o in bpy.context.scene.objects:
        o.select_set(o in targets)
    bpy.context.view_layer.objects.active = targets[0]
    path = os.path.join(STATE["dir"], data["resultObj"])
    tmp = path + ".tmp.obj"
    bpy.ops.wm.obj_export(filepath=tmp, export_selected_objects=True, apply_modifiers=True, export_materials=False,
                          export_colors=True, export_uv=True, export_normals=True, export_triangulated_mesh=True,
                          forward_axis="NEGATIVE_Z", up_axis="Y")
    os.replace(tmp, path)
    return len(targets), 0


def send_to_engine():
    data = STATE["data"]
    if data is None:
        return "No NLSDFE exchange in this session"
    count, skipped = export_sdf(data) if data["kind"] == "sdf" else export_mesh(data)
    message = "Sent %d object(s) to NLSDFE" % count
    if skipped:
        message += " (%d ignored: not engine primitives)" % skipped
    return message


@bpy.app.handlers.persistent
def on_save_post(*_):
    if STATE["quiet_save"]:   # Первое сохранение при запуске — сцена ещё не менялась
        STATE["quiet_save"] = False
        return
    if STATE["data"] is not None:
        print("[NLSDFE]", send_to_engine())


# ---------------------------------------------------------------------------
# Интерфейс в Blender: панель N > NLSDFE
# ---------------------------------------------------------------------------
class NLSDFE_OT_send(bpy.types.Operator):
    bl_idname = "nlsdfe.send"
    bl_label = "Send to NLSDFE"
    bl_description = "Write the result for the engine (Ctrl+S does the same)"

    def execute(self, context):
        self.report({"INFO"}, send_to_engine())
        return {"FINISHED"}


class NLSDFE_OT_add(bpy.types.Operator):
    bl_idname = "nlsdfe.add_primitive"
    bl_label = "Add SDF Primitive"
    bl_options = {"REGISTER", "UNDO"}
    kind: bpy.props.EnumProperty(items=TYPES[:-1], name="Shape")

    def execute(self, context):
        mesh = bpy.data.meshes.new(self.kind)
        obj = bpy.data.objects.new(self.kind.capitalize(), mesh)
        context.collection.objects.link(obj)
        obj.nls_managed = True
        obj.nls_id = -1
        obj.nls_type = self.kind
        obj.location = context.scene.cursor.location
        build_primitive(mesh, obj.nls_type, obj.nls_size, obj.nls_p2)
        set_color(obj, [0.8, 0.8, 0.8], 0.0, 0.5)
        for o in context.selected_objects:
            o.select_set(False)
        obj.select_set(True)
        context.view_layer.objects.active = obj
        return {"FINISHED"}


class NLSDFE_PT_panel(bpy.types.Panel):
    bl_label = "NLSDFE"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "NLSDFE"

    def draw(self, context):
        layout = self.layout
        data = STATE["data"]
        layout.label(text="Mesh edit" if data and data["kind"] == "mesh" else "SDF model edit")
        layout.operator("nlsdfe.send", icon="EXPORT")
        layout.label(text="Ctrl+S also sends")
        obj = context.active_object
        if data and data["kind"] == "sdf":
            layout.separator()
            layout.operator_menu_enum("nlsdfe.add_primitive", "kind", text="Add SDF Primitive", icon="ADD")
            if obj and obj.nls_managed:
                box = layout.box()
                box.prop(obj, "nls_type")
                if obj.nls_type != "volume":
                    box.prop(obj, "nls_size")
                if obj.nls_type in ("torus", "capsule", "cone"):
                    box.prop(obj, "nls_p2")
                box.prop(obj, "nls_op")
                box.prop(obj, "nls_smooth")
                box.label(text="Colour: the material Base Color")
            elif obj:
                layout.label(text="Not an engine primitive: ignored", icon="INFO")


CLASSES = (NLSDFE_OT_send, NLSDFE_OT_add, NLSDFE_PT_panel)


def register():
    O = bpy.types.Object
    O.nls_managed = bpy.props.BoolProperty(default=False)
    O.nls_mesh = bpy.props.BoolProperty(default=False)
    O.nls_id = bpy.props.IntProperty(default=-1)
    O.nls_type = bpy.props.EnumProperty(items=TYPES, name="Shape", default="sphere", update=on_shape_changed)
    O.nls_size = bpy.props.FloatProperty(name="Size", default=1.0, min=0.001, update=on_shape_changed)
    O.nls_p2 = bpy.props.FloatProperty(name="Shape ratio", default=0.3, min=0.0, max=1.0, update=on_shape_changed)
    O.nls_op = bpy.props.EnumProperty(items=OPS, name="Operation", default="union", update=on_op_changed)
    O.nls_smooth = bpy.props.FloatProperty(name="Smoothness", default=0.1, min=0.0, max=2.0)
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    if on_save_post not in bpy.app.handlers.save_post:
        bpy.app.handlers.save_post.append(on_save_post)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        print("[NLSDFE] usage: blender --python nlsdfe_link.py -- <exchange.json> [--selftest]")
        return
    exchange = os.path.abspath(argv[0])
    with open(exchange) as f:
        data = json.load(f)
    STATE.update(exchange=exchange, data=data, dir=os.path.dirname(exchange))
    register()

    clear_scene()
    if data["kind"] == "sdf":
        build_sdf(data)
    else:
        build_mesh(data)

    if "--selftest" in argv:
        print("[NLSDFE]", send_to_engine())
        return
    # Файл .blend рядом с обменом: Ctrl+S сохраняет без диалога и отдаёт результат движку
    blend = os.path.join(STATE["dir"], data.get("blend", "edit.blend"))
    STATE["quiet_save"] = True
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    STATE["quiet_save"] = False


main()
