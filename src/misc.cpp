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

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <openssl/evp.h>
#include <pwd.h>

#include "configuration.h"
#include "misc.h"

extern ScrobbyConfig config;

size_t write_data(char *buffer, size_t size, size_t nmemb, std::string data)
{
	size_t result = size * nmemb;
	data.append(buffer, result);
	return result;
}

void ChangeToUser()
{
	if (config.dedicated_user.empty() || getuid() != 0)
		return;
	
	passwd *userinfo;
	if (!(userinfo = getpwnam(config.dedicated_user.c_str())))
	{
		std::cerr << "user " << config.dedicated_user << " not found!\n";
		exit(1);
	}
	if (setgid(userinfo->pw_gid) == -1)
	{
		std::cerr << "cannot set gid for user " << config.dedicated_user << ": " << strerror(errno) << std::endl;
		exit(1);
	}
	if (setuid(userinfo->pw_uid) == -1)
	{
		std::cerr << "cannot change to uid of user " << config.dedicated_user << ": " << strerror(errno) << std::endl;
		exit(1);
	}
	config.user_home_folder = userinfo->pw_dir ? userinfo->pw_dir : "";
}

bool Daemonize()
{
	if (daemon(0, 0) < 0)
		return false;
	
	std::ofstream f(config.file_pid.c_str(), std::ios_base::trunc);
	if (f.is_open())
	{
		pid_t pid = getpid();
		f << pid;
		f.close();
		return true;
	}
	else
		return false;
}

void ClearCache()
{
	std::ofstream f(config.file_cache.c_str(), std::ios::trunc);
	f.close();
}

void GetCachedSongs(std::vector<std::string> &v)
{
	std::ifstream f(config.file_cache.c_str());
	if (f.is_open())
	{
		std::string line;
		while (!f.eof())
		{
			getline(f, line);
			if (!line.empty())
				v.push_back(line);
		}
	}
}

void Cache(const std::string &s)
{
	std::ofstream f(config.file_cache.c_str(), std::ios::app);
	if (f.is_open())
	{
		f << s << std::endl;
		f.close();
	}
}

void Log(LogLevel ll, const char *format, ...)
{
	if (config.log_level < ll)
		return;
	FILE *f = fopen(config.file_log.c_str(), "a");
	if (!f)
	{
		perror("Cannot open log file!\n");
		exit(1);
	}
	fprintf(f, "[%s] ", DateTime().c_str());
	va_list list;
	va_start(list, format);
	vfprintf(f, format, list);
	va_end(list);
	fprintf(f, "\n");
	fclose(f);
}

void ignore_newlines(std::string &s)
{
	for (size_t i = s.find("\n"); i != std::string::npos; i = s.find("\n"))
		s.replace(i, 1, "");
}

std::string md5sum(const std::string &s)
{
	unsigned char md_value[EVP_MAX_MD_SIZE];
	unsigned int md_len;
	
	EVP_MD_CTX mdctx;
	EVP_DigestInit(&mdctx, EVP_md5());
	EVP_DigestUpdate(&mdctx, s.c_str(), s.length());
	EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
	EVP_MD_CTX_cleanup(&mdctx);
	
	char result[md_len*2+1];
	for (unsigned i = 0; i < md_len; i++)
		sprintf(&result[i*2], "%02x", md_value[i]);
	result[md_len*2] = 0;
	
	return result;
}

std::string DateTime()
{
	char result[32];
	time_t raw;
	tm *t;
	time(&raw);
	t = localtime(&raw);
	result[strftime(result, 31, "%Y/%m/%d %X", t)] = 0;
	return result;
}

int StrToInt(const std::string &s)
{
	return atoi(s.c_str());
}

