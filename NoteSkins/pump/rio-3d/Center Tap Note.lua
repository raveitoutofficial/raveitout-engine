-- TEST: beveled 3D tap note built from the 2D rio art
-- (tools/noteskin3d_prep.py + tools/noteskin3d_blender.py --lit).
-- Lit like In the Groove's 3D noteskins: the engine lights the model, so its
-- edges catch highlight and shadow as it turns.
-- Rocks on the beat (wag: 20 deg on X, 25 deg on Y, one rock per 2 beats). Kept on purpose (2026-11-25).
return Def.ActorFrame {
	Lighting=true;
	InitCommand=cmd(SetAmbientLightColor,color("0.55,0.55,0.55,1");SetDiffuseLightColor,color("0.6,0.6,0.6,1");SetSpecularLightColor,color("1,1,1,1");SetLightDirection,{-0.45,-0.55,0.7});
	Def.Model {
		Meshes=NOTESKIN:GetPath('_Center','model');
		Materials=NOTESKIN:GetPath('_Center','model');
		Bones=NOTESKIN:GetPath('_Center','model');
		InitCommand=cmd(wag;effectmagnitude,20,25,0;effectperiod,2;effectclock,"beat");
	};
};
