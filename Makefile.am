# not a GNU package. You can remove this line, if
# have all needed files, that a GNU package needs
AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS = src doc

servicedir = $(libdir)/systemd/system
service_DATA = systemd/scrobby.service

tmpfddir = $(libdir)/tmpfiles.d
tmpfd_DATA = systemd/scrobby.conf
