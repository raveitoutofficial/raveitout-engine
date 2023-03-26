#include "global.h"
#include "CodeSet.h"
#include "ThemeManager.h"
#include "InputEventPlus.h"
#include "MessageManager.h"
//#include "RageLog.h"

#define CODE_NAMES		THEME->GetMetric (sType,"CodeNames")
#define CODE( s )		THEME->GetMetric (sType,ssprintf("Code%s",s.c_str()))
void InputQueueCodeSet::Load( const RString &sType )
{
	/*
	 * Split the CodeNames into a vector. A string like
	 * "Down=Down,Up=Up,Up2=Up,Up3=Up,Up4=Up,Left1=Left,Left2=Left,Right1=Right,Right2=Right,Null,Start2=Start"
	 * Will become a vector ["Down=Down","Up=Up","Up2="Up","Up3=Up",...]
     */
	split( CODE_NAMES, ",", m_asCodeNames, true );

	for( unsigned c=0; c<m_asCodeNames.size(); c++ )
	{
		vector<RString> asBits;
		split( m_asCodeNames[c], "=", asBits, true );
		RString sCodeName = asBits[0];

        //If there was =, assign it back to the CodeNames
        //Essentially m_asCodeNames[c] = m_asCodeNames[c].split("=")[1]
        //So "Up2=Up" becomes "Up"
		if( asBits.size() > 1 )
			m_asCodeNames[c] = asBits[1];

		InputQueueCode code;
        code.Load(CODE(sCodeName)); //Load the name defined to the left of =, or just the name if = isn't present
        /* Good programmers will notice that this causes every remaining code to be
         * off by one because the vector sizes no longer match. There's no reason for it to
         * skip in the first place because an invalid code will just be empty, so DON'T
         * UNCOMMENT IT!
         */
		/*if( !code.Load(CODE(sCodeName)) ) //Load the name defined to the left of =, or just the name if = isn't present
			continue;*/

        //LOG->Trace("Mapped %s to %s",CODE(sCodeName).c_str(),sCodeName.c_str());
		m_aCodes.push_back( code );
	}
}

RString InputQueueCodeSet::Input( const InputEventPlus &input ) const
{
	for( unsigned i = 0; i < m_aCodes.size(); ++i )
	{
		if( !m_aCodes[i].EnteredCode(input.GameI.controller) )
			continue;

		return m_asCodeNames[i];
	}
	return "";
}

bool InputQueueCodeSet::InputMessage( const InputEventPlus &input, Message &msg ) const
{
	RString sCodeName = Input( input );
	if( sCodeName.empty() )
		return false;

	msg.SetName("Code");
	msg.SetParam( "PlayerNumber", input.pn );
	msg.SetParam( "Name", sCodeName );
	return true;
}
/*  (c) 2023 Accelerator.

    This file is part of SM-RIO.

    SM-RIO is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    SM-RIO is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with SM-RIO.  If not, see <https://www.gnu.org/licenses/>.

    This file incorporates work covered by the following copyright and
    permission notice:

     * (c) 2007 Glenn Maynard
     * All rights reserved.
     *
     * Permission is hereby granted, free of charge, to any person obtaining a
     * copy of this software and associated documentation files (the
     * "Software"), to deal in the Software without restriction, including
     * without limitation the rights to use, copy, modify, merge, publish,
     * distribute, and/or sell copies of the Software, and to permit persons to
     * whom the Software is furnished to do so, provided that the above
     * copyright notice(s) and this permission notice appear in all copies of
     * the Software and that both the above copyright notice(s) and this
     * permission notice appear in supporting documentation.
     *
     * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
     * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
     * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
     * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
     * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
     * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
     * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
     * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
     * PERFORMANCE OF THIS SOFTWARE.
 */
