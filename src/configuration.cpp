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

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>

#ifdef __linux__
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#elif defined(__FreeBSD__)
#include <fcntl.h>
#include <kvm.h>
#include <paths.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "configuration.h"
#include "misc.h"

using std::string;

namespace {
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
	
	void HomeFolder(const ScrobbyConfig &conf, string &s)
	{
		if (s[0] == '~')
			s.replace(0, 1, conf.user_home_folder);
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

void ParseArgv(ScrobbyConfig &conf, int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--help") == 0)
		{
			std::cout
			<< "usage:\n"
			<< "scrobby [options] <conf file>\n"
			<< "scrobby [options] (search for ~/.scrobbyconf, then /etc/scrobby.conf)\n\n"
			<< "options:\n"
			<< "   --help                show this help message\n"
			<< "   --no-daemon           do not detach from console\n"
			<< "   --quiet               do not log anything\n"
			<< "   --verbose             verbose logging\n"
			<< "   --version             print version information\n"
			;
			exit(0);
		}
		else if (strcmp(argv[i], "--no-daemon") == 0)
		{
			conf.daemonize = false;
		}
		else if (strcmp(argv[i], "--quiet") == 0)
		{
			conf.log_level = llNone;
		}
		else if (strcmp(argv[i], "--verbose") == 0)
		{
			conf.log_level = llVerbose;
		}
		else if (strcmp(argv[i], "--version") == 0)
		{
			std::cout << "scrobby - an audioscrobbler mpd client, version "VERSION"\n";
			exit(0);
		}
		else
		{
			conf.file_config = argv[i];
		}
	}
}

bool CheckFiles(ScrobbyConfig &conf)
{
	std::ofstream f;
	std::ifstream g;
	
	g.open(conf.file_pid.c_str());
	if (g.is_open())
	{
		string strpid;
		getline(g, strpid);
		g.close();
		pid_t pid = StrToInt(strpid);
		if (pid < 1)
		{
			std::cerr << "pid file: " << conf.file_pid << " is invalid, trying to remove...\n";
			if (unlink(conf.file_pid.c_str()) == 0)
			{
				std::cout << "pid file succesfully removed.\n";
			}
			else
			{
				std::cerr << "couldn't remove pid file.\n";
				return false;
			}
		}
		else
		{
#			ifdef __linux__
			struct stat stat_pid;
			std::ostringstream proc_file;
			proc_file << "/proc/" << pid << std::ends;
			if (!(stat(proc_file.str().c_str(), &stat_pid) == -1 && errno == ENOENT))
			{
#			elif defined(__FreeBSD__)
			char kvm_errbuf[_POSIX2_LINE_MAX];
			kvm_t *hkvm;
			int cnt = 0;
			struct kinfo_proc *proc;
			
			hkvm = kvm_openfiles(NULL, _PATH_DEVNULL, NULL, O_RDONLY, kvm_errbuf);
			if (hkvm)
			{
				proc = kvm_getprocs(hkvm, KERN_PROC_PID, pid, &cnt);
				kvm_close(hkvm);
			}
			else
			{
				std::cerr << "kvm_openfiles failed with message:" << std::endl << kvm_errbuf << std::endl;
			}
			if (cnt >= 1)
			{
#			endif
				std::cerr << "scrobby is already running with PID " << pid << "!\n";
				return false;
			}
		}
	}
	
	f.open(conf.file_pid.c_str(), std::ios_base::app);
	if (!f.is_open())
	{
		std::cerr << "Cannot create/open pid file: " << conf.file_pid << std::endl;
		return false;
	}
	f.close();
	
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
	
	return true;
}

void DefaultConfiguration(ScrobbyConfig &conf)
{
	conf.user_home_folder = getenv("HOME") ? getenv("HOME") : "";
	
	conf.mpd_host = "localhost";
	conf.mpd_port = 6600;
	conf.mpd_timeout = 15;
	
	conf.file_log = "/var/log/scrobby/scrobby.log";
	conf.file_pid = "/var/run/scrobby/scrobby.pid";
	conf.file_cache = "/var/cache/scrobby/scrobby.cache";
	
	conf.log_level = llUndefined;
	conf.daemonize = true;
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
			
			if (line.find("dedicated_user") != string::npos)
			{
				if (!line.empty())
					conf.dedicated_user = v;
			}
			else if (line.find("mpd_host") != string::npos)
			{
				if (!line.empty())
					conf.mpd_host = v;
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
				{
					HomeFolder(conf, v);
					conf.file_log = v;
				}
			}
			else if (line.find("pid_file") != string::npos)
			{
				if (!v.empty())
				{
					HomeFolder(conf, v);
					conf.file_pid = v;
				}
			}
			else if (line.find("cache_file") != string::npos)
			{
				if (!v.empty())
				{
					HomeFolder(conf, v);
					conf.file_cache = v;
				}
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
				if (!v.empty() && conf.log_level == llUndefined)
					conf.log_level = IntoLogLevel(v);
			}
		}
	}
	f.close();
	return true;
}

