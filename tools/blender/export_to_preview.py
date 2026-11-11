# =========================================================
# ファイルの情報[export_to_preview.py]
#
# 制作者:Masatora Tanaka        日付：2026/11/11
# =========================================================
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy

bl_info = {
    "name": "GM31 Export to Preview",
    "author": "Masatora Tanaka",
    "version": (1, 0, 0),
    "blender": (4, 2, 0),
    "location": "3D View > Sidebar > Game Preview",
    "category": "Import-Export",
}


class GM31PreviewSettings(bpy.types.PropertyGroup):
    project_folder: bpy.props.StringProperty(
        name="Game project", subtype="DIR_PATH",
        description="Folder containing GM31_Game.vcxproj",
    )
    character_collection: bpy.props.PointerProperty(
        name="Character collection", type=bpy.types.Collection,
    )


class GM31_OT_export_preview(bpy.types.Operator):
    bl_idname = "gm31.export_preview"
    bl_label = "Export to Preview"

    @classmethod
    def poll(cls, context):
        return context.mode == "OBJECT"

    def execute(self, context):
        settings = context.scene.gm31_preview
        if not settings.project_folder or settings.character_collection is None:
            self.report({"ERROR"}, "Set the game project and character collection.")
            return {"CANCELLED"}

        project = Path(bpy.path.abspath(settings.project_folder))
        if not (project / "GM31_Game.vcxproj").is_file():
            self.report({"ERROR"}, "Choose the folder containing GM31_Game.vcxproj.")
            return {"CANCELLED"}

        # コレクションのメッシュとアーマチュアだけを書き出す。
        objects = [
            obj for obj in settings.character_collection.all_objects
            if obj.type in {"MESH", "ARMATURE"}
        ]
        armatures = [obj for obj in objects if obj.type == "ARMATURE"]
        meshes = [obj for obj in objects if obj.type == "MESH"]
        if len(armatures) != 1 or not meshes:
            self.report({"ERROR"}, "Use one armature and at least one mesh.")
            return {"CANCELLED"}
        if any(obj.name not in context.view_layer.objects or not obj.visible_get() for obj in objects):
            self.report({"ERROR"}, "Make all character meshes and the armature visible in this view layer.")
            return {"CANCELLED"}
        if any(obj.find_armature() not in (None, armatures[0]) for obj in meshes):
            self.report({"ERROR"}, "All skinned meshes must use the collection's armature.")
            return {"CANCELLED"}

        output = project / "asset" / "model" / "Player_Movement" / "Player_Preview.fbx"
        try:
            # 出力が完了した時点で差し替え、失敗時は前の FBX を残す。
            with TemporaryDirectory(dir=output.parent, prefix="gm31_export_") as temporary:
                staged = Path(temporary) / output.name
                with context.temp_override(selected_objects=objects):
                    result = bpy.ops.export_scene.fbx(
                        filepath=str(staged),
                        use_selection=True,
                        object_types={"MESH", "ARMATURE"},
                        axis_forward="-Z",
                        axis_up="Y",
                        use_mesh_modifiers=False,
                        add_leaf_bones=False,
                        bake_anim=False,
                        path_mode="COPY",
                        embed_textures=True,
                    )
                if "FINISHED" not in result or not staged.is_file():
                    self.report({"ERROR"}, "FBX export did not finish.")
                    return {"CANCELLED"}
                staged.replace(output)
        except (OSError, RuntimeError) as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        shape_names = {
            key.name
            for mesh in meshes if mesh.data.shape_keys
            for key in list(mesh.data.shape_keys.key_blocks)[1:]
        }
        self.report({"INFO"}, f"Exported Player_Preview.fbx ({len(shape_names)} Blender shape keys).")
        return {"FINISHED"}


class GM31_PT_preview(bpy.types.Panel):
    bl_label = "Game Preview"
    bl_idname = "GM31_PT_preview"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Game Preview"

    def draw(self, context):
        settings = context.scene.gm31_preview
        self.layout.prop(settings, "project_folder")
        self.layout.prop(settings, "character_collection")
        self.layout.operator("gm31.export_preview", icon="EXPORT")


classes = (GM31PreviewSettings, GM31_OT_export_preview, GM31_PT_preview)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.gm31_preview = bpy.props.PointerProperty(type=GM31PreviewSettings)


def unregister():
    del bpy.types.Scene.gm31_preview
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()

