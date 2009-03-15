/***************************************************************************
 *   Copyright (C) 2008-2009 by Andrzej Rybczak                            *
 *   electricityispower@gmail.com                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#ifndef _SONG_H
#define _SONG_H

#include <queue>
#include <deque>

#include "libmpdclient.h"

namespace MPD
{
	class Song
	{
		public:
			Song();
			~Song();
			
			void SetData(mpd_Song *);
			void Submit();
			bool isStream() const;
			
			mpd_Song *Data;
			time_t StartTime;
			int Playback;
			
			static void LockQueue() { pthread_mutex_lock(&itsQueueMutex); }
			static void UnlockQueue() { pthread_mutex_unlock(&itsQueueMutex); }
			
			static void GetCached();
			static void ExtractQueue();
			
			static bool SendQueue();
			
			static std::queue<MPD::Song> Queue;
			static std::deque<std::string> SubmitQueue;
			
		private:
			void Clear();
			
			static pthread_mutex_t itsSubmitQueueMutex;
			static pthread_mutex_t itsQueueMutex;
			
			bool canBeSubmitted();
			bool itsIsStream;
	};
}

#endif
