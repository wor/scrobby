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

#include <iostream>
#include <fstream>

#include "configuration.h"
#include "misc.h"

using std::string;

namespace
{
	std::string GetLineValue(const string &line, char a = '"', char b = '"')
	{
		int i = 0;
		int begin = -1, end = -1;
		for (string::const_iterator it = line.begin(); it != line.end(); i++, it++)
		{
			if (*it == a || *it == b)
			{
				if (begin < 0)
					begin = i+1;
				else
					end = i;
			}
		}
		if (begin >= 0 && end >= 0)
			return line.substr(begin, end-begin);
		else
			return "";
	}
	
	LogLevel IntoLogLevel(const string &value)
	{
		LogLevel result = llInfo;
		if (value == "none")
		{
			result = llNone;
		}
		else if (value == "info")
		{
			result = llInfo;
		}
		else if (value == "verbose")
		{
			result = llVerbose;
		}
		return result;
	}
	
}

bool CheckFiles(ScrobbyConfig &conf)
{
	std::ofstream f;
	
	f.open(conf.file_log.c_str(), std::ios_base::app);
	if (!f.is_open())
	{
		std::cerr << "Cannot create/open log file: " << conf.file_log << std::endl;
		return false;
	}
	f.close();
	
	f.open(conf.file_cache.c_str(), std::ios_base::app);
	if (!f.is_open())
	{
		std::cerr << "Cannot create/open cache file: " << conf.file_cache << std::endl;
		return false;
	}
	f.close();
	
	std::ifstream g(conf.file_pid.c_str());
	if (g.is_open())
	{
		string pid;
		getline(g, pid);
		std::cerr << "scrobby is already running with PID " << pid << "!\n";
		return false;
	}
	f.open(conf.file_pid.c_str(), std::ios_base::app);
	if (!f.is_open())
	{
		std::cerr << "Cannot create/open pid file: " << conf.file_pid << std::endl;
		return false;
	}
	f.close();
	
	return true;
}

void DefaultConfiguration(ScrobbyConfig &conf)
{
	conf.mpd_host = "localhost";
	conf.mpd_port = 6600;
	conf.mpd_timeout = 15;
	
	conf.file_log = "/var/log/scrobby.log";
	conf.file_pid = "/var/run/scrobby.pid";
	conf.file_cache = "/var/cache/scrobby/scrobby.cache";
	
	conf.log_level = llInfo;
}

bool ReadConfiguration(ScrobbyConfig &conf, const string &file)
{
	string line, v;
	std::ifstream f(file.c_str());
	
	if (!f.is_open())
		return false;
	
	while (!f.eof())
	{
		getline(f, line);
		if (!line.empty() && line[0] != '#')
		{
			v = GetLineValue(line);
			
			if (line.find("mpd_host") != string::npos)
			{
				if (!line.empty())
					conf.mpd_host = v;
			}
			else if (line.find("mpd_password") != string::npos)
			{
				if (!v.empty())
					conf.mpd_password = v;
			}
			else if (line.find("mpd_port") != string::npos)
			{
				if (!v.empty())
					conf.mpd_port = StrToInt(v);
			}
			else if (line.find("mpd_timeout") != string::npos)
			{
				if (!v.empty())
					conf.mpd_timeout = StrToInt(v);
			}
			else if (line.find("log_file") != string::npos)
			{
				if (!v.empty())
					conf.file_log = v;
			}
			else if (line.find("pid_file") != string::npos)
			{
				if (!v.empty())
					conf.file_pid = v;
			}
			else if (line.find("cache_file") != string::npos)
			{
				if (!v.empty())
					conf.file_cache = v;
			}
			else if (line.find("lastfm_user") != string::npos)
			{
				if (!v.empty())
					conf.lastfm_user = v;
			}
			else if (line.find("lastfm_password") != string::npos)
			{
				if (!v.empty())
					conf.lastfm_password = v;
			}
			else if (line.find("lastfm_md5_password") != string::npos)
			{
				if (!v.empty())
					conf.lastfm_md5_password = v;
			}
			else if (line.find("log_level") != string::npos)
			{
				if (!v.empty())
					conf.log_level = IntoLogLevel(v);
			}
		}
	}
	f.close();
	return true;
}

