return Def.ActorFrame{
	Def.Quad{
		InitCommand=cmd(setsize,65,150;vertalign,bottom;diffuse,1,1,1,.5;fadetop,1;y,60);
		GainFocusCommand=cmd(stoptweening;decelerate,.1;diffuse,1,1,1,.5;);
		LoseFocusCommand=cmd(stoptweening;decelerate,.1;diffuse,.5,.5,.5,.5;);
	};
	LoadFont("Common Normal")..{
		Text=Var('Button');
		InitCommand=cmd(y,50;maxwidth,60;Stroke,Color.Black;shadowlength,1);
		GainFocusCommand=cmd(diffuse,Color.HoloBlue);
		LoseFocusCommand=cmd(diffuse,Color.White);
	};
	NOTESKIN:LoadActorForNoteSkin(Var('Button'),"Tap Note","default")..{
		GainFocusCommand=cmd(diffusealpha,1);
		LoseFocusCommand=cmd(diffusealpha,.5);
	}
}
