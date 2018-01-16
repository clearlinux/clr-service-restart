
EXTRA_DIST = clr-service-restart.1.md

clr-service-restart.1: clr-service-restart.1.md
	ronn -r $< --pipe > $@

bin_PROGRAMS = clr-service-restart

clr_service_restart_SOURCES = \
	main.c

dist_man_MANS = \
	clr-service-restart.1
