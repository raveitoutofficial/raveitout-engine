-- TEST: beveled 3D tap note built from the 2D rio art
-- (tools/noteskin3d_prep.py + tools/noteskin3d_blender.py --lit).
-- Lit like In the Groove's 3D noteskins: the engine lights the model, so its
-- edges catch highlight and shadow as it turns.
return Def.ActorFrame {
	Lighting=true;
	InitCommand=cmd(SetAmbientLightColor,color("0.55,0.55,0.55,1");SetDiffuseLightColor,color("0.6,0.6,0.6,1");SetSpecularLightColor,color("1,1,1,1");SetLightDirection,{-0.45,-0.55,0.7});
	Def.Model {
		Meshes=NOTESKIN:GetPath('_UpLeft','model');
		Materials=NOTESKIN:GetPath('_UpLeft','model');
		Bones=NOTESKIN:GetPath('_UpLeft','model');
	};
};
