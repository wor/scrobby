/***************************************************************************
 *   Copyright (C) 2008 by Andrzej Rybczak   *
 *   electricityispower@gmail.com   *
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
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef _CONFIGURATION_H
#define _CONFIGURATION_H

#include <string>

enum LogLevel { llUndefined, llNone, llInfo, llVerbose };

struct ScrobbyConfig
{
	std::string dedicated_user;
	
	std::string mpd_host;
	int mpd_port;
	int mpd_timeout;
	
	std::string file_config;
	std::string file_log;
	std::string file_pid;
	std::string file_cache;
	
	std::string lastfm_user;
	std::string lastfm_password;
	std::string lastfm_md5_password;
	
	LogLevel log_level;
	bool daemonize;
};

void ParseArgv(ScrobbyConfig &, int, char **);
bool CheckFiles(ScrobbyConfig &);
void DefaultConfiguration(ScrobbyConfig &);
bool ReadConfiguration(ScrobbyConfig &, const std::string &file);


#endif

